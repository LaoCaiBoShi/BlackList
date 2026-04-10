#define _CRT_SECURE_NO_WARNINGS

#include "blacklist_checker.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <thread>
#include <atomic>

// 包含 simdjson 库头文件
#include "simdjson.h"

// Windows 特定头文件
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

/**
 * @brief 构造函数
 */
BlacklistChecker::BlacklistChecker() {
    // 初始化版本信息
    std::fill(versionInfo.begin(), versionInfo.end(), ' ');
}

/**
 * @brief 提取卡片号1-4位（省份+运营商）
 * @param cardId 卡片号
 * @return 前缀代码
 */
unsigned short BlacklistChecker::getPrefixCode(const std::string& cardId) {
    if (cardId.length() < 4) return 0;
    return std::stoi(cardId.substr(0, 4));
}

/**
 * @brief 提取卡片类型（9-10位）
 * @param cardId 卡片号
 * @return 卡片类型
 */
unsigned short BlacklistChecker::getCardType(const std::string& cardId) {
    if (cardId.length() < 10) return 0;
    return std::stoi(cardId.substr(8, 2));
}

/**
 * @brief 提取年份（5-6位）
 * @param cardId 卡片号
 * @return 年份
 */
unsigned short BlacklistChecker::getYear(const std::string& cardId) {
    if (cardId.length() < 6) return 0;
    return std::stoi(cardId.substr(4, 2));
}

/**
 * @brief 提取星期数（7-8位）
 * @param cardId 卡片号
 * @return 星期数
 */
unsigned short BlacklistChecker::getWeek(const std::string& cardId) {
    if (cardId.length() < 8) return 0;
    return std::stoi(cardId.substr(6, 2));
}

/**
 * @brief 提取内部编号（11-20位）
 * @param cardId 卡片号
 * @return 内部编号
 */
std::string BlacklistChecker::getInnerId(const std::string& cardId) {
    if (cardId.length() < 20) return "";
    return cardId.substr(10, 10);
}

/**
 * @brief 加载黑名单
 * @param filename 文件名
 * @return 加载是否成功
 */
bool BlacklistChecker::loadFromFile(const std::string& filename) {
    return loadFromFileToMap(filename, cardMap, versionInfo);
}

/**
 * @brief 定期更新黑名单（并行加载，完成后切换）
 * @param filename 文件名
 * @return 更新是否成功
 */
bool BlacklistChecker::updateFromFile(const std::string& filename) {
    // 并行加载到临时map
    std::unordered_map<unsigned short, std::unordered_map<unsigned short, std::vector<CardInfo>>> tempMap;
    std::array<char, 6> tempVersion;
    
    if (!loadFromFileToMap(filename, tempMap, tempVersion)) {
        return false;
    }
    
    // 完成后切换（简单实现，实际应用中可能需要更复杂的同步机制）
    cardMap.swap(tempMap);
    versionInfo = tempVersion;
    
    // 重建布隆过滤器
    bloomFilter.clear();
    
    // 将所有卡号添加到布隆过滤器
    for (const auto& prefixEntry : cardMap) {
        for (const auto& typeEntry : prefixEntry.second) {
            for (const auto& card : typeEntry.second) {
                // 构建完整卡片号
                std::string fullCardId = std::to_string(prefixEntry.first);
                
                // 年份（2位）
                std::string yearStr = std::to_string(card.getYear());
                if (yearStr.length() < 2) yearStr = "0" + yearStr;
                fullCardId += yearStr;
                
                // 星期数（2位）
                std::string weekStr = std::to_string(card.getWeek());
                if (weekStr.length() < 2) weekStr = "0" + weekStr;
                fullCardId += weekStr;
                
                // 卡片类型（2位）
                std::string typeStr = std::to_string(typeEntry.first);
                if (typeStr.length() < 2) typeStr = "0" + typeStr;
                fullCardId += typeStr;
                
                // 内部编号（10位）
                fullCardId += card.getInnerIdStr();
                
                bloomFilter.add(fullCardId);
            }
        }
    }
    
    return true;
}

/**
 * @brief 添加到黑名单
 * @param cardId 卡片号
 */
void BlacklistChecker::add(const std::string& cardId) {
    if (cardId.length() != 20) return;
    
    unsigned short prefix = getPrefixCode(cardId);
    unsigned short type = getCardType(cardId);
    unsigned short year = getYear(cardId);
    unsigned short week = getWeek(cardId);
    std::string innerId = getInnerId(cardId);
    
    CardInfo cardInfo(year, week, innerId);
    
    // 使用互斥锁保护cardMap的并发访问
    std::lock_guard<std::mutex> lock(cardMapMutex);
    
    // 添加到map
    cardMap[prefix][type].push_back(cardInfo);
    
    // 暂时禁用排序，提高加载性能
    // std::sort(cardMap[prefix][type].begin(), cardMap[prefix][type].end());
    
    // 添加到布隆过滤器
    bloomFilter.add(cardId);
}

/**
 * @brief 批量添加到黑名单（减少锁竞争）
 * @param cardIds 卡号列表
 */
void BlacklistChecker::addBatch(const std::vector<std::string>& cardIds) {
    // 预先分组数据，减少锁竞争
    std::unordered_map<unsigned short, std::unordered_map<unsigned short, std::vector<CardInfo>>> localMap;
    std::vector<std::string> localCardIds;

    // 预处理：将数据按prefix和type分组
    for (const std::string& cardId : cardIds) {
        if (cardId.length() != 20) continue;

        unsigned short prefix = getPrefixCode(cardId);
        unsigned short type = getCardType(cardId);
        unsigned short year = getYear(cardId);
        unsigned short week = getWeek(cardId);
        std::string innerId = getInnerId(cardId);

        CardInfo cardInfo(year, week, innerId);
        localMap[prefix][type].push_back(cardInfo);
        localCardIds.push_back(cardId);
    }

    // 一次性加锁，批量合并数据
    std::lock_guard<std::mutex> lock(cardMapMutex);

    // 合并到全局map
    for (auto& prefixEntry : localMap) {
        for (auto& typeEntry : prefixEntry.second) {
            auto& targetVec = cardMap[prefixEntry.first][typeEntry.first];
            targetVec.insert(targetVec.end(), typeEntry.second.begin(), typeEntry.second.end());
        }
    }
    
    // 添加到布隆过滤器
    for (const auto& cardId : localCardIds) {
        bloomFilter.add(cardId);
    }
}

/**
 * @brief 从黑名单中删除
 * @param cardId 卡片号
 */
void BlacklistChecker::remove(const std::string& cardId) {
    if (cardId.length() != 20) return;
    
    unsigned short prefix = getPrefixCode(cardId);
    unsigned short type = getCardType(cardId);
    unsigned short year = getYear(cardId);
    unsigned short week = getWeek(cardId);
    std::string innerId = getInnerId(cardId);
    
    CardInfo cardInfo(year, week, innerId);
    
    // 从map中删除
    auto prefixIt = cardMap.find(prefix);
    if (prefixIt != cardMap.end()) {
        auto typeIt = prefixIt->second.find(type);
        if (typeIt != prefixIt->second.end()) {
            auto& cards = typeIt->second;
            auto cardIt = std::find(cards.begin(), cards.end(), cardInfo);
            if (cardIt != cards.end()) {
                cards.erase(cardIt);
                
                // 如果该类型下没有卡片，删除该类型
                if (cards.empty()) {
                    prefixIt->second.erase(typeIt);
                }
                
                // 如果该前缀下没有类型，删除该前缀
                if (prefixIt->second.empty()) {
                    cardMap.erase(prefixIt);
                }
            }
        }
    }
    
    // 注意：布隆过滤器不支持删除操作
    // 实际应用中可能需要重建布隆过滤器
}

/**
 * @brief 检查是否在黑名单
 * @param cardId 卡片号
 * @return 是否在黑名单中
 */
bool BlacklistChecker::isBlacklisted(const std::string& cardId) {
    if (cardId.length() != 20) return false;
    
    // 先检查布隆过滤器
    bool bloomResult = bloomFilter.contains(cardId);
    std::cout << "Bloom filter check: " << (bloomResult ? "POSITIVE" : "NEGATIVE") << std::endl;
    
    if (!bloomResult) {
        return false;
    }
    
    // 布隆过滤器检查通过，再检查分层存储
    unsigned short prefix = getPrefixCode(cardId);
    unsigned short type = getCardType(cardId);
    unsigned short year = getYear(cardId);
    unsigned short week = getWeek(cardId);
    std::string innerId = getInnerId(cardId);
    
    CardInfo cardInfo(year, week, innerId);
    
    // 使用互斥锁保护cardMap的并发读取
    std::lock_guard<std::mutex> lock(cardMapMutex);
    
    auto prefixIt = cardMap.find(prefix);
    if (prefixIt != cardMap.end()) {
        auto typeIt = prefixIt->second.find(type);
        if (typeIt != prefixIt->second.end()) {
            const auto& cards = typeIt->second;
            bool storageResult = std::binary_search(cards.begin(), cards.end(), cardInfo);
            std::cout << "Storage check: " << (storageResult ? "BLACKLISTED" : "NOT BLACKLISTED") << std::endl;
            return storageResult;
        }
    }
    
    std::cout << "Storage check: NOT BLACKLISTED" << std::endl;
    return false;
}

/**
 * @brief 获取黑名单大小
 * @return 黑名单大小
 */
size_t BlacklistChecker::size() const {
    size_t count = 0;
    for (const auto& prefixEntry : cardMap) {
        for (const auto& typeEntry : prefixEntry.second) {
            count += typeEntry.second.size();
        }
    }
    return count;
}

/**
 * @brief 保存到文件
 * @param filename 文件名
 * @return 保存是否成功
 */
bool BlacklistChecker::saveToFile(const std::string& filename) {
    try {
        std::ofstream file(filename);
        if (!file.is_open()) {
            return false;
        }
        
        // 写入版本信息
        for (char c : versionInfo) {
            file << c;
        }
        file << '\n';
        
        // 写入卡片数据
        for (const auto& prefixEntry : cardMap) {
            for (const auto& typeEntry : prefixEntry.second) {
                for (const auto& card : typeEntry.second) {
                    // 构建完整卡片号
                    std::string fullCardId = std::to_string(prefixEntry.first);
                    
                    // 年份（2位）
                    std::string yearStr = std::to_string(card.getYear());
                    if (yearStr.length() < 2) yearStr = "0" + yearStr;
                    fullCardId += yearStr;
                    
                    // 星期数（2位）
                    std::string weekStr = std::to_string(card.getWeek());
                    if (weekStr.length() < 2) weekStr = "0" + weekStr;
                    fullCardId += weekStr;
                    
                    // 卡片类型（2位）
                    std::string typeStr = std::to_string(typeEntry.first);
                    if (typeStr.length() < 2) typeStr = "0" + typeStr;
                    fullCardId += typeStr;
                    
                    // 内部编号（10位）
                    fullCardId += card.getInnerIdStr();
                    
                    file << fullCardId << '\n';
                }
            }
        }
        
        file.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error saving blacklist: " << e.what() << std::endl;
        return false;
    }
}

/**
 * @brief 加载黑名单到指定map
 * @param filename 文件名
 * @param targetMap 目标map
 * @param versionOut 版本信息输出
 * @return 加载是否成功
 */
bool BlacklistChecker::loadFromFileToMap(const std::string& filename, std::unordered_map<unsigned short, std::unordered_map<unsigned short, std::vector<CardInfo>>>& targetMap, std::array<char, 6>& versionOut) {
    try {
        std::ifstream file(filename);
        if (!file.is_open()) {
            return false;
        }
        
        // 读取版本信息
        std::string versionLine;
        if (std::getline(file, versionLine)) {
            for (size_t i = 0; i < 6 && i < versionLine.length(); ++i) {
                versionOut[i] = versionLine[i];
            }
        }
        
        // 读取卡片数据
        std::string line;
        while (std::getline(file, line)) {
            if (line.length() == 20) {
                unsigned short prefix = getPrefixCode(line);
                unsigned short type = getCardType(line);
                unsigned short year = getYear(line);
                unsigned short week = getWeek(line);
                std::string innerId = getInnerId(line);
                
                CardInfo cardInfo(year, week, innerId);
                targetMap[prefix][type].push_back(cardInfo);
            }
        }
        
        file.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error loading blacklist: " << e.what() << std::endl;
        return false;
    }
}

/**
 * @brief 设置版本信息
 * @param version 版本信息
 */
void BlacklistChecker::setVersionInfo(const std::string& version) {
    for (size_t i = 0; i < 6 && i < version.length(); ++i) {
        versionInfo[i] = version[i];
    }
}

/**
 * @brief 获取版本信息
 * @return 版本信息
 */
std::string BlacklistChecker::getVersionInfo() const {
    return std::string(versionInfo.begin(), versionInfo.end());
}

/**
 * @brief 从指定文件地址读取txt格式的黑名单
 * @param filename 文件名
 * @param loadedCount 成功加载的数量
 * @param invalidCount 无效卡片ID数量
 * @return 加载是否成功
 */
bool BlacklistChecker::loadTxtBlacklist(const std::string& filename, size_t& loadedCount, size_t& invalidCount) {
    try {
        std::ifstream file(filename);
        if (!file.is_open()) {
            return false;
        }
        
        loadedCount = 0;
        invalidCount = 0;
        
        std::string line;
        while (std::getline(file, line)) {
            // 去除首尾空格
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);
            
            if (line.length() == 20) {
                add(line);
                loadedCount++;
            } else {
                invalidCount++;
            }
        }
        
        file.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error loading txt blacklist: " << e.what() << std::endl;
        return false;
    }
}

/**
 * @brief 从指定文件地址读取JSON格式的黑名单
 * @param filename 文件名
 * @param loadedCount 成功加载的数量
 * @param invalidCount 无效卡片ID数量
 * @return 加载是否成功
 */
bool BlacklistChecker::loadFromJsonFile(const std::string& filename, size_t& loadedCount, size_t& invalidCount) {
    try {
        // 打开文件
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }

        // 一次性读取整个文件内容
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        loadedCount = 0;
        invalidCount = 0;

        // 使用 simdjson 解析 JSON
        simdjson::dom::parser parser;
        auto json_result = parser.parse(content);

        if (json_result.error()) {
            return false;
        }

        simdjson::dom::element json = json_result.value();

        // 检查是否是数组
        if (!json.is_array()) {
            return false;
        }

        simdjson::dom::array array = json.get_array();

        // 遍历数组中的每个元素
        for (simdjson::dom::element element : array) {
            // 获取 cardId 字段
            simdjson::dom::object obj = element.get_object();
            simdjson::dom::element cardIdElement;
            simdjson::error_code error = obj["cardId"].get(cardIdElement);
            if (error) {
                continue;
            }

            std::string_view cardIdView;
            error = cardIdElement.get(cardIdView);
            if (error) {
                continue;
            }

            std::string cardId(cardIdView);
            if (cardId.length() == 20) {
                // 验证卡号格式（纯数字）
                bool valid = true;
                for (char c : cardId) {
                    if (c < '0' || c > '9') {
                        valid = false;
                        break;
                    }
                }

                if (valid) {
                    add(cardId);
                    loadedCount++;
                } else {
                    invalidCount++;
                }
            } else {
                invalidCount++;
            }
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error loading json blacklist: " << e.what() << std::endl;
        return false;
    }
}

/**
 * @brief 预分配容量
 * @param additionalRecords 额外记录数
 */
void BlacklistChecker::reserveCapacity(size_t additionalRecords) {
    // 预留空间，避免频繁扩容
    // 实际应用中可能需要更复杂的预留策略
}

/**
 * @brief 对所有数据进行排序
 */
void BlacklistChecker::sortAll() {
    // 使用互斥锁保护cardMap的并发访问
    std::lock_guard<std::mutex> lock(cardMapMutex);

    // 对每个前缀下的每个类型的卡片进行排序
    for (auto& prefixEntry : cardMap) {
        for (auto& typeEntry : prefixEntry.second) {
            std::sort(typeEntry.second.begin(), typeEntry.second.end());
        }
    }
}

/**
 * @brief 获取系统剩余内存（MB）
 * @return 剩余内存
 */
size_t BlacklistChecker::getAvailableMemory() {
#ifdef _WIN32
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);
    return memInfo.ullAvailPhys / (1024 * 1024);
#else
    // 非Windows平台的实现
    return 0;
#endif
}

/**
 * @brief 计算当前黑名单占用的内存（MB）
 * @return 内存占用
 */
size_t BlacklistChecker::getCurrentMemoryUsage() {
    // 估算内存占用
    size_t size = 0;
    
    // 计算cardMap的内存占用
    for (const auto& prefixEntry : cardMap) {
        // 前缀条目：键（2字节） + 值（unordered_map）
        size += sizeof(prefixEntry.first);
        
        for (const auto& typeEntry : prefixEntry.second) {
            // 类型条目：键（2字节） + 值（vector<CardInfo>）
            size += sizeof(typeEntry.first);
            
            // CardInfo：year_week（2字节） + innerId（8字节）
            size += typeEntry.second.size() * (sizeof(unsigned short) + sizeof(unsigned long long));
        }
    }
    
    // 转换为MB
    return size / (1024 * 1024);
}

/**
 * @brief 获取布隆过滤器中的元素数量
 * @return 布隆过滤器中的元素数量
 */
size_t BlacklistChecker::getBloomFilterElementCount() const {
    return bloomFilter.getElementCount();
}

/**
 * @brief 输出加载统计信息
 */
void BlacklistChecker::printLoadingStats() const {
    std::cout << "==========================================" << std::endl;
    std::cout << " Loading Statistics " << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "Total blacklist loaded: " << size() << std::endl;
    std::cout << "Bloom filter elements: " << bloomFilter.getElementCount() << std::endl;
    std::cout << "==========================================" << std::endl;
}
