#ifndef BLACKLIST_CHECKER_H
#define BLACKLIST_CHECKER_H

#include <unordered_map>
#include <array>
#include <vector>
#include <string>
#include <algorithm>
#include <bitset>
#include <memory>
#include <cmath>
#include <iostream>
#include <mutex>

// 前向声明，避免循环依赖
class PersistReader;

class BlacklistChecker {
    friend class PersistReader;

public:
    // 卡片号结构：存储5-6位（年份）、7-8位（星期数）和11-20位（内部编号）
    // 公开给 PersistReader 使用
    struct CardInfo {
        unsigned short year_week; // 高8位存储year，低8位存储week
        unsigned long long innerId; // 11-20位：内部编号（位压缩存储）

        CardInfo() : year_week(0), innerId(0) {}

        CardInfo(unsigned short y, unsigned short w, const std::string& innerIdStr) {
            // 编码year和week到year_week
            year_week = ((y / 10) << 12) | ((y % 10) << 8) | ((w / 10) << 4) | (w % 10);
            
            // 编码innerId（11-20位）到位压缩格式
            innerId = 0;
            for (size_t i = 0; i < 10; i++) {
                if (i < innerIdStr.length()) {
                    int digit = innerIdStr[i] - '0';
                    innerId |= (static_cast<unsigned long long>(digit) << (i * 4));
                }
            }
        }
        
        // 获取year
        unsigned short getYear() const {
            return ((year_week >> 12) & 0x0F) * 10 + ((year_week >> 8) & 0x0F);
        }
        
        // 获取week
        unsigned short getWeek() const {
            return ((year_week >> 4) & 0x0F) * 10 + (year_week & 0x0F);
        }
        
        // 获取innerId字符串
        std::string getInnerIdStr() const {
            std::string result;
            for (int i = 0; i < 10; ++i) {
                int digit = (innerId >> (i * 4)) & 0x0F;
                result += (digit + '0');
            }
            return result;
        }
        
        // 比较运算符，用于排序和查找
        bool operator<(const CardInfo& other) const {
            if (year_week != other.year_week) {
                return year_week < other.year_week;
            }
            return innerId < other.innerId;
        }
        
        bool operator==(const CardInfo& other) const {
            return year_week == other.year_week && innerId == other.innerId;
        }
        
        bool operator!=(const CardInfo& other) const {
            return !(*this == other);
        }
    };
    
    // 布隆过滤器类
    class BloomFilter {
    private:
        // 最大位数配置：8000万数据，误判率<十万分之一
        // 最优位数公式：m = -n × ln(p) / (ln(2)²)
        // n=80M, p=0.00001 → m ≈ 19.2亿 bits ≈ 230 MB
        // 考虑数据存储约550MB，总计约780MB < 800MB限制
        static const size_t MAX_BITS = 2000000000; // 最大位数（约240MB）
        size_t m_bits; // 布隆过滤器的位数
        size_t m_hashCount; // 哈希函数数量
        std::unique_ptr<std::vector<bool>> bits; // 使用vector<bool>代替bitset，支持动态大小
        mutable std::mutex m_mutex; // 互斥锁，保护bits的并发访问
        size_t m_elementCount; // 元素数量计数器

        // 哈希函数
        inline size_t hash(const std::string& key, size_t seed) {
            size_t hash = seed;
            for (char c : key) {
                hash = hash * 31 + static_cast<unsigned char>(c);
            }
            return hash % m_bits;
        }

    public:
        /**
         * @brief 构造布隆过滤器
         * @param expectedElements 预期元素数量
         * @param falsePositiveRate 期望的误判率
         */
        BloomFilter(size_t expectedElements = 1000000, double falsePositiveRate = 0.001) {
            // 计算最优位数
            m_bits = static_cast<size_t>(-expectedElements * std::log(falsePositiveRate) / (std::log(2) * std::log(2)));
            
            // 限制最大位数，避免内存分配失败
            if (m_bits > MAX_BITS) {
                m_bits = MAX_BITS;
                std::cout << "BloomFilter: Bits limit reached, using " << MAX_BITS << " bits" << std::endl;
            }
            
            // 确保位数至少为1000
            m_bits = std::max(m_bits, static_cast<size_t>(1000));
            
            // 计算最优哈希函数数量
            m_hashCount = static_cast<size_t>(m_bits / expectedElements * std::log(2));
            // 确保哈希函数数量至少为1，最多为10
            m_hashCount = std::max(m_hashCount, static_cast<size_t>(1));
            m_hashCount = std::min(m_hashCount, static_cast<size_t>(10));
            
            // 初始化元素计数器
            m_elementCount = 0;
            
            try {
                // 初始化位向量
                bits = std::make_unique<std::vector<bool>>(m_bits, false);
                std::cout << "BloomFilter initialized with " << m_bits << " bits and " << m_hashCount << " hash functions" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "BloomFilter initialization failed: " << e.what() << std::endl;
                // 使用默认大小
                m_bits = 1000000; // 约122KB
                m_hashCount = 3;
                bits = std::make_unique<std::vector<bool>>(m_bits, false);
                std::cout << "BloomFilter initialized with default settings: " << m_bits << " bits and " << m_hashCount << " hash functions" << std::endl;
            }
        }
        
        void add(const std::string& key) {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (size_t i = 0; i < m_hashCount; ++i) {
                size_t index = hash(key, i);
                if (index < m_bits) {
                    (*bits)[index] = true;
                }
            }
            m_elementCount++;
        }
        
        bool contains(const std::string& key) {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (size_t i = 0; i < m_hashCount; ++i) {
                size_t index = hash(key, i);
                if (index >= m_bits || !(*bits)[index]) {
                    return false;
                }
            }
            return true;
        }
        
        void clear() {
            std::lock_guard<std::mutex> lock(m_mutex);
            std::fill(bits->begin(), bits->end(), false);
            m_elementCount = 0;
        }
        
        // 获取当前布隆过滤器的位数
        size_t getBits() const {
            return m_bits;
        }
        
        // 获取当前哈希函数数量
        size_t getHashCount() const {
            return m_hashCount;
        }
        
        // 获取元素数量
        size_t getElementCount() const {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_elementCount;
        }
    };

    // 省份代码最大值（省级行政区划代码最大约65）
    static constexpr size_t MAX_PROVINCE_CODE = 100;

    // 省份分片数据结构
    // 利用卡号前2位=省份代码的天然映射，实现O(1)分片定位
    struct ProvinceShard {
        std::array<std::vector<CardInfo>, 3> cards;
        mutable std::mutex mutex;

        ProvinceShard() = default;
    };

    // 省份分片数组 - 按省份代码直接索引
    // 省份代码 11 -> shards[11] (北京)
    // 查询时：卡号前2位 -> 直接定位分片
    std::array<ProvinceShard, MAX_PROVINCE_CODE> provinceShards;

    // 根据省份代码获取分片索引
    inline size_t getShardIndex(unsigned short provinceCode) const {
        return provinceCode < MAX_PROVINCE_CODE ? provinceCode : 0;
    }

    // 根据卡号前2位获取省份代码
    inline unsigned short getProvinceCode(const std::string& cardId) const {
        if (cardId.length() < 2) return 0;
        return std::stoi(cardId.substr(0, 2));
    }

    // 布隆过滤器，用于快速判断卡片是否可能在黑名单中
    BloomFilter bloomFilter;

    // 黑名单版本信息（6字节）
    std::array<char, 6> versionInfo;

    // 提取卡片号1-4位（省份+运营商）
    unsigned short getPrefixCode(const std::string& cardId);

    // 提取卡片类型（9-10位）
    unsigned short getCardType(const std::string& cardId);
    
    // 为指定省份预分配容量
    // @param provinceCode 省份代码 (0-99)
    // @param jsonFileCount JSON文件数量
    // @param cardsPerJson 每JSON文件预估卡数 (默认1000)
    // @param bufferFactor 缓冲系数 (默认1.2)
    // @return 是否预分配成功
    bool reserveProvinceCapacitySafe(int provinceCode, 
                                    size_t jsonFileCount,
                                    size_t cardsPerJson = 1000,
                                    double bufferFactor = 1.2);
    
    // 对指定省份的卡片进行排序
    void sortProvince(int provinceCode);

    // 获取卡片类型在数组中的索引
    // cardType 22 -> 0, cardType 23 -> 1, 其他 -> 2
    inline size_t getTypeIndex(unsigned short cardType) const {
        if (cardType == 22) return 0;
        if (cardType == 23) return 1;
        return 2;
    }

    // 提取年份（5-6位）
    unsigned short getYear(const std::string& cardId);

    // 提取星期数（7-8位）
    unsigned short getWeek(const std::string& cardId);

    // 提取内部编号（11-20位）
    std::string getInnerId(const std::string& cardId);

    // 加载黑名单到指定map
    bool loadFromFileToMap(const std::string& filename, std::unordered_map<unsigned short, std::array<std::vector<CardInfo>, 3>>& targetMap, std::array<char, 6>& versionOut);
    
    // 获取系统剩余内存（MB）
    size_t getAvailableMemory();
    
    // 计算当前黑名单占用的内存（MB）
    size_t getCurrentMemoryUsage();
    
public:
    // 构造函数
    BlacklistChecker();
    
    // 加载黑名单
    bool loadFromFile(const std::string& filename);
    
    // 定期更新黑名单（并行加载，完成后切换）
    bool updateFromFile(const std::string& filename);
    
    // 添加到黑名单
    void add(const std::string& cardId);

    // 批量添加到黑名单（减少锁竞争）
    void addBatch(const std::vector<std::string>& cardIds);
    
    // 从黑名单中删除
    void remove(const std::string& cardId);
    
    // 检查是否在黑名单
    bool isBlacklisted(const std::string& cardId);
    
    // 获取黑名单大小
    size_t size() const;
    
    // 保存到文件
    bool saveToFile(const std::string& filename);
    
    // 设置版本信息
    void setVersionInfo(const std::string& version);
    
    // 获取版本信息
    std::string getVersionInfo() const;
    
    // 从指定文件地址读取txt格式的黑名单
    bool loadTxtBlacklist(const std::string& filename, size_t& loadedCount, size_t& invalidCount);
    
    // 从指定文件地址读取JSON格式的黑名单
    bool loadFromJsonFile(const std::string& filename, size_t& loadedCount, size_t& invalidCount);
    
    // 预分配容量
    void reserveCapacity(size_t additionalRecords);
    
    // 对所有数据进行排序
    void sortAll();
    
    // 获取布隆过滤器中的元素数量
    size_t getBloomFilterElementCount() const;

    // 输出加载统计信息
    void printLoadingStats() const;

    // ==================== 持久化相关接口 ====================

    // 持久化文件格式版本
    static const uint32_t PERSIST_FORMAT_VERSION = 1;

    // 持久化文件Header结构（64字节）
    struct PersistHeader {
        char magic[4];               // 0-3: "BLCK" 魔数
        uint32_t version;            // 4-7: 格式版本
        uint32_t prefixCount;       // 8-11: 省份数量
        uint32_t totalCards;        // 12-15: 总卡片数
        uint64_t bloomFilterBits;   // 16-23: 布隆过滤器位数
        uint64_t createdTime;       // 24-31: 创建时间戳
        char versionInfo[6];        // 32-37: 版本信息
        char reserved[26];          // 38-63: 保留字段
    };

    // 省份索引条目（32字节）
    struct PersistIndexEntry {
        uint16_t prefix;            // 省份代码
        uint16_t reserved;          // 保留
        uint32_t type0Count;       // type22 数量
        uint64_t type0Offset;       // type22 数据偏移
        uint32_t type1Count;        // type23 数量
        uint64_t type1Offset;       // type23 数据偏移
        uint32_t type2Count;        // 其他类型数量
        uint64_t type2Offset;       // 其他类型数据偏移
    };

    // 保存为持久化格式（二进制，快速加载）
    // 文件结构：Header + IndexTable + BinaryCardData
    bool saveToPersistFile(const std::string& filename);

    // 从持久化格式加载（快速恢复）
    // 使用内存映射文件（mmap）实现秒级加载
    bool loadFromPersistFile(const std::string& filename);

    // 优先从持久化文件恢复，如果不存在或损坏则从原始数据加载
    // dataPath: 原始数据路径（如 ZIP 文件目录）
    // persistPath: 持久化文件路径
    // 注意：此方法假设调用方已经通过 loadFromJsonFile/loadTxtBlacklist 等方法
    // 加载了原始数据到 cardMap，仅用于在原始数据加载后保存持久化文件
    // 如需在启动时自动恢复，请使用 loadFromPersistFile + 外部加载逻辑的组合
    bool savePersistAfterLoad(const std::string& persistPath);

    // 检查持久化文件是否存在且有效
    bool isPersistFileValid(const std::string& persistPath);
};

#endif // BLACKLIST_CHECKER_H
