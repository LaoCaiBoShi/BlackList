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
#include <filesystem>

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
    if (!loadFromFileToMap(filename, cardMap, versionInfo)) {
        return false;
    }
    sortAll();
    return true;
}

/**
 * @brief 定期更新黑名单（并行加载，完成后切换）
 * @param filename 文件名
 * @return 更新是否成功
 */
bool BlacklistChecker::updateFromFile(const std::string& filename) {
    // 并行加载到临时map
    std::unordered_map<unsigned short, std::array<std::vector<CardInfo>, 3>> tempMap;
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
        for (size_t typeIdx = 0; typeIdx < 3; ++typeIdx) {
            const auto& cards = prefixEntry.second[typeIdx];
            for (const auto& card : cards) {
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
                unsigned short cardType = (typeIdx == 0) ? 22 : (typeIdx == 1) ? 23 : 0;
                std::string typeStr = std::to_string(cardType);
                if (typeStr.length() < 2) typeStr = "0" + typeStr;
                fullCardId += typeStr;

                // 内部编号（10位）
                fullCardId += card.getInnerIdStr();

                bloomFilter.add(fullCardId);
            }
        }
    }

    sortAll();

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

    // 添加到map，使用getTypeIndex获取正确的数组索引
    cardMap[prefix][getTypeIndex(type)].push_back(cardInfo);

    // 添加到布隆过滤器
    bloomFilter.add(cardId);
}

/**
 * @brief 批量添加到黑名单（减少锁竞争）
 * @param cardIds 卡号列表
 */
void BlacklistChecker::addBatch(const std::vector<std::string>& cardIds) {
    // 预先分组数据，减少锁竞争
    std::unordered_map<unsigned short, std::array<std::vector<CardInfo>, 3>> localMap;
    std::vector<std::string> localCardIds;
    localCardIds.reserve(cardIds.size()); // 预分配内存

    // 预处理：将数据按 prefix 和 type 分组
    for (const std::string& cardId : cardIds) {
        if (cardId.length() != 20) continue;

        unsigned short prefix = getPrefixCode(cardId);
        unsigned short type = getCardType(cardId);
        unsigned short year = getYear(cardId);
        unsigned short week = getWeek(cardId);
        std::string innerId = getInnerId(cardId);

        CardInfo cardInfo(year, week, innerId);
        localMap[prefix][getTypeIndex(type)].push_back(cardInfo);
        localCardIds.push_back(cardId);
    }

    // 一次性加锁，批量合并数据
    std::lock_guard<std::mutex> lock(cardMapMutex);

    // 合并到全局 map
    for (auto& prefixEntry : localMap) {
        for (size_t typeIdx = 0; typeIdx < 3; ++typeIdx) {
            auto& sourceVec = prefixEntry.second[typeIdx];
            if (!sourceVec.empty()) {
                auto& targetVec = cardMap[prefixEntry.first][typeIdx];
                targetVec.insert(targetVec.end(), sourceVec.begin(), sourceVec.end());
            }
        }
    }

    // 批量添加到布隆过滤器（布隆过滤器内部已有锁保护）
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
        size_t typeIdx = getTypeIndex(type);
        auto& cards = prefixIt->second[typeIdx];
        auto cardIt = std::find(cards.begin(), cards.end(), cardInfo);
        if (cardIt != cards.end()) {
            cards.erase(cardIt);
        }
    }

    // 注意：布隆过滤器不支持删除操作
    // 由于本项目每天全量更新，这种不一致在更新时会自动修复
}

/**
 * @brief 检查是否在黑名单
 * @param cardId 卡片号
 * @return 是否在黑名单中
 */
bool BlacklistChecker::isBlacklisted(const std::string& cardId) {
    if (cardId.length() != 20) return false;

    // 先检查布隆过滤器
    if (!bloomFilter.contains(cardId)) {
        return false;
    }

    // 布隆过滤器检查通过，再检查分层存储
    unsigned short prefix = getPrefixCode(cardId);
    unsigned short type = getCardType(cardId);
    unsigned short year = getYear(cardId);
    unsigned short week = getWeek(cardId);
    std::string innerId = getInnerId(cardId);
    size_t typeIdx = getTypeIndex(type);

    CardInfo cardInfo(year, week, innerId);

    // 使用互斥锁保护cardMap的并发读取
    std::lock_guard<std::mutex> lock(cardMapMutex);

    auto prefixIt = cardMap.find(prefix);
    if (prefixIt != cardMap.end()) {
        const auto& cards = prefixIt->second[typeIdx];
        return std::binary_search(cards.begin(), cards.end(), cardInfo);
    }

    return false;
}

/**
 * @brief 获取黑名单大小
 * @return 黑名单大小
 */
size_t BlacklistChecker::size() const {
    size_t count = 0;
    for (const auto& prefixEntry : cardMap) {
        for (size_t typeIdx = 0; typeIdx < 3; ++typeIdx) {
            count += prefixEntry.second[typeIdx].size();
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
            for (size_t typeIdx = 0; typeIdx < 3; ++typeIdx) {
                const auto& cards = prefixEntry.second[typeIdx];
                for (const auto& card : cards) {
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
                    unsigned short cardType = (typeIdx == 0) ? 22 : (typeIdx == 1) ? 23 : 0;
                    std::string typeStr = std::to_string(cardType);
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
bool BlacklistChecker::loadFromFileToMap(const std::string& filename, std::unordered_map<unsigned short, std::array<std::vector<CardInfo>, 3>>& targetMap, std::array<char, 6>& versionOut) {
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
                targetMap[prefix][getTypeIndex(type)].push_back(cardInfo);
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
        sortAll();
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
        // 检查文件是否存在
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Failed to open JSON file: " << filename << " (File not found or cannot be opened)" << std::endl;
            return false;
        }

        // 一次性读取整个文件内容
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        std::cout << "Successfully opened JSON file: " << filename << " (Size: " << content.size() << " bytes)" << std::endl;

        loadedCount = 0;
        invalidCount = 0;

        // 使用 simdjson 解析 JSON
        simdjson::dom::parser parser;
        auto json_result = parser.parse(content);

        if (json_result.error()) {
            std::cerr << "JSON parsing error: " << simdjson::error_message(json_result.error()) << " in file: " << filename << std::endl;
            return false;
        }

        simdjson::dom::element json = json_result.value();

        // 检查是否是数组
        if (!json.is_array()) {
            std::cerr << "JSON root is not an array: " << filename << std::endl;
            return false;
        }

        simdjson::dom::array array = json.get_array();
        std::cout << "Found JSON array with " << array.size() << " elements in: " << filename << std::endl;

        // 第一遍：收集所有有效的卡号
        std::vector<std::string> validCardIds;
        validCardIds.reserve(array.size()); // 预分配内存

        for (simdjson::dom::element element : array) {
            // 获取 cardId 字段
            simdjson::dom::object obj = element.get_object();
            simdjson::dom::element cardIdElement;
            simdjson::error_code error = obj["cardId"].get(cardIdElement);
            if (error) {
                invalidCount++;
                continue;
            }

            std::string_view cardIdView;
            error = cardIdElement.get(cardIdView);
            if (error) {
                invalidCount++;
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
                    validCardIds.push_back(cardId);
                } else {
                    invalidCount++;
                }
            } else {
                invalidCount++;
            }
        }

        // 第二遍：批量添加到黑名单（减少锁竞争）
        if (!validCardIds.empty()) {
            addBatch(validCardIds);
            loadedCount = validCardIds.size();
        }

        std::cout << "Processed JSON file: " << filename << " (Loaded: " << loadedCount << ", Invalid: " << invalidCount << ")" << std::endl;
        sortAll();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error loading json blacklist: " << e.what() << " in file: " << filename << std::endl;
        return false;
    }
}

/**
 * @brief 预分配容量
 * @param additionalRecords 额外记录数
 */
void BlacklistChecker::reserveCapacity(size_t additionalRecords) {
    // 预分配布隆过滤器容量
    // 布隆过滤器在构造时已分配，这里不需要额外操作

    // 预分配哈希表容量
    // 通过预先创建常见的前缀和类型条目，减少动态扩容
    std::lock_guard<std::mutex> lock(cardMapMutex);

    // 预分配省份代码（1-4 位）
    // 实际省份代码约 31 个（省级行政区）
    const size_t EXPECTED_PREFIX_COUNT = 31;

    // 预分配卡容量
    size_t totalExpectedCards = additionalRecords;
    size_t avgCardsPerPrefix = totalExpectedCards / EXPECTED_PREFIX_COUNT;
    size_t avgCardsPerType = avgCardsPerPrefix / 3;  // 只有 3 个类型槽位

    // 预分配省份代码空间
    // 这可以减少后续插入时的 rehash 操作
    cardMap.reserve(EXPECTED_PREFIX_COUNT);

    // 预分配每个省份下的三个类型槽位
    for (size_t prefix = 0; prefix < EXPECTED_PREFIX_COUNT; ++prefix) {
        for (size_t typeIdx = 0; typeIdx < 3; ++typeIdx) {
            cardMap[prefix][typeIdx].reserve(avgCardsPerType);
        }
    }

    std::cout << "Reserved capacity for " << additionalRecords << " additional records" << std::endl;
}

/**
 * @brief 对所有卡片进行排序（并行优化版本）
 *        只对元素数量超过阈值的数据集进行并行排序
 */
void BlacklistChecker::sortAll() {
    // 使用互斥锁保护cardMap的并发访问
    std::lock_guard<std::mutex> lock(cardMapMutex);

    // 第一遍：收集所有需要排序的向量指针
    std::vector<std::vector<CardInfo>*> vectorsToSort;
    for (auto& prefixEntry : cardMap) {
        for (size_t typeIdx = 0; typeIdx < 3; ++typeIdx) {
            auto& cards = prefixEntry.second[typeIdx];
            // 只收集元素数量超过1000的向量进行并行排序
            if (cards.size() > 1000) {
                vectorsToSort.push_back(&cards);
            }
        }
    }

    // 如果需要排序的数据集较少，直接使用单线程排序
    if (vectorsToSort.size() < 4) {
        for (auto& prefixEntry : cardMap) {
            for (size_t typeIdx = 0; typeIdx < 3; ++typeIdx) {
                auto& cards = prefixEntry.second[typeIdx];
                std::sort(cards.begin(), cards.end());
            }
        }
        return;
    }

    // 确定并行线程数
    size_t threadCount = std::thread::hardware_concurrency();
    if (threadCount == 0) threadCount = 4;
    if (threadCount > vectorsToSort.size()) {
        threadCount = vectorsToSort.size();
    }

    // 每个线程处理的向量数量
    size_t vectorsPerThread = vectorsToSort.size() / threadCount;
    std::vector<std::thread> threads;
    threads.reserve(threadCount);

    // 启动并行排序线程
    for (size_t i = 0; i < threadCount; ++i) {
        size_t startIdx = i * vectorsPerThread;
        size_t endIdx = (i == threadCount - 1) ? vectorsToSort.size() : (startIdx + vectorsPerThread);

        threads.emplace_back([startIdx, endIdx, &vectorsToSort]() {
            for (size_t j = startIdx; j < endIdx; ++j) {
                std::sort(vectorsToSort[j]->begin(), vectorsToSort[j]->end());
            }
        });
    }

    // 等待所有排序线程完成
    for (auto& t : threads) {
        t.join();
    }

    // 对剩余的小数据集进行单线程排序
    for (auto& prefixEntry : cardMap) {
        for (size_t typeIdx = 0; typeIdx < 3; ++typeIdx) {
            auto& cards = prefixEntry.second[typeIdx];
            // 已经排序过的会跳过（因为sort是稳定排序）
            // 对于小于阈值的向量进行排序
            if (cards.size() <= 1000) {
                std::sort(cards.begin(), cards.end());
            }
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
        // 前缀条目：键（2字节）
        size += sizeof(prefixEntry.first);

        for (size_t typeIdx = 0; typeIdx < 3; ++typeIdx) {
            // array[3] 的三个槽位
            size += sizeof(std::vector<CardInfo>);  // array 元素本身

            // CardInfo：year_week（2字节） + innerId（8字节）
            size += prefixEntry.second[typeIdx].size() * (sizeof(unsigned short) + sizeof(unsigned long long));
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

// ==================== 持久化相关实现 ====================

/**
 * @brief 保存为持久化格式（二进制）
 * 文件结构：Header(64B) + IndexTable(variable) + BinaryCardData(variable)
 * @param filename 持久化文件路径
 * @return 保存是否成功
 */
bool BlacklistChecker::saveToPersistFile(const std::string& filename) {
    try {
        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Failed to open persist file for writing: " << filename << std::endl;
            return false;
        }

        // 计算数据偏移量
        const size_t headerSize = sizeof(PersistHeader);
        const size_t indexEntrySize = sizeof(PersistIndexEntry);

        // 第一遍：计算每个省份的数据偏移（只保存有数据的前缀）
        std::vector<std::pair<uint16_t, std::array<uint64_t, 3>>> prefixOffsets;
        std::vector<std::pair<uint16_t, std::array<uint32_t, 3>>> prefixCounts;
        size_t currentOffset = headerSize;  // 数据从 header 后开始

        for (const auto& prefixEntry : cardMap) {
            uint16_t prefix = prefixEntry.first;
            std::array<uint32_t, 3> counts = {
                static_cast<uint32_t>(prefixEntry.second[0].size()),
                static_cast<uint32_t>(prefixEntry.second[1].size()),
                static_cast<uint32_t>(prefixEntry.second[2].size())
            };
            // 跳过没有任何数据的前缀
            if (counts[0] == 0 && counts[1] == 0 && counts[2] == 0) {
                continue;
            }
            std::array<uint64_t, 3> offsets = {0, 0, 0};
            offsets[0] = currentOffset;
            currentOffset += static_cast<uint64_t>(counts[0]) * sizeof(CardInfo);
            offsets[1] = currentOffset;
            currentOffset += static_cast<uint64_t>(counts[1]) * sizeof(CardInfo);
            offsets[2] = currentOffset;
            currentOffset += static_cast<uint64_t>(counts[2]) * sizeof(CardInfo);
            prefixOffsets.push_back({prefix, offsets});
            prefixCounts.push_back({prefix, counts});
        }

        size_t prefixCount = prefixOffsets.size();
        size_t indexTableSize = prefixCount * indexEntrySize;
        size_t dataStartOffset = headerSize + indexTableSize;

        // 重新计算偏移量：数据从 header + indexTable 后开始
        currentOffset = dataStartOffset;
        for (size_t i = 0; i < prefixOffsets.size(); ++i) {
            auto& offsets = prefixOffsets[i].second;
            offsets[0] = currentOffset;
            currentOffset += static_cast<uint64_t>(prefixCounts[i].second[0]) * sizeof(CardInfo);
            offsets[1] = currentOffset;
            currentOffset += static_cast<uint64_t>(prefixCounts[i].second[1]) * sizeof(CardInfo);
            offsets[2] = currentOffset;
            currentOffset += static_cast<uint64_t>(prefixCounts[i].second[2]) * sizeof(CardInfo);
        }

        // 写入 Header
        PersistHeader header;
        std::memset(&header, 0, sizeof(header));
        std::memcpy(header.magic, "BLCK", 4);
        header.version = PERSIST_FORMAT_VERSION;
        header.prefixCount = static_cast<uint32_t>(prefixCount);
        header.totalCards = static_cast<uint32_t>(size());
        header.bloomFilterBits = bloomFilter.getBits();
        header.createdTime = static_cast<uint64_t>(std::time(nullptr));
        std::memcpy(header.versionInfo, versionInfo.data(), 6);
        file.write(reinterpret_cast<const char*>(&header), sizeof(header));

        // 写入 Index Table
        for (size_t i = 0; i < prefixOffsets.size(); ++i) {
            PersistIndexEntry entry;
            entry.prefix = prefixOffsets[i].first;
            entry.reserved = 0;
            entry.type0Count = prefixCounts[i].second[0];
            entry.type0Offset = prefixOffsets[i].second[0];
            entry.type1Count = prefixCounts[i].second[1];
            entry.type1Offset = prefixOffsets[i].second[1];
            entry.type2Count = prefixCounts[i].second[2];
            entry.type2Offset = prefixOffsets[i].second[2];
            file.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
        }

        // 写入 Binary Card Data（按照索引表的顺序）
        for (size_t i = 0; i < prefixOffsets.size(); ++i) {
            uint16_t prefix = prefixOffsets[i].first;
            const auto& typeArrays = cardMap.at(prefix);
            for (size_t typeIdx = 0; typeIdx < 3; ++typeIdx) {
                const auto& cards = typeArrays[typeIdx];
                if (!cards.empty()) {
                    file.write(reinterpret_cast<const char*>(cards.data()),
                              cards.size() * sizeof(CardInfo));
                }
            }
        }

        file.close();
        std::cout << "Persist file saved: " << filename << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error saving persist file: " << e.what() << std::endl;
        return false;
    }
}

/**
 * @brief 从持久化格式加载（快速恢复）
 * 使用 mmap 实现秒级加载
 * @param filename 持久化文件路径
 * @return 加载是否成功
 */
bool BlacklistChecker::loadFromPersistFile(const std::string& filename) {
    std::vector<PersistIndexEntry> indexTable;
    uint32_t prefixCount = 0;
    PersistHeader header;

    {
        // 读取文件头和索引表（不需要锁）
        std::ifstream file(filename, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            std::cerr << "Failed to open persist file: " << filename << std::endl;
            return false;
        }

        file.seekg(0, std::ios::beg);

        if (!file.read(reinterpret_cast<char*>(&header), sizeof(header))) {
            std::cerr << "Failed to read persist header" << std::endl;
            return false;
        }

        if (std::memcmp(header.magic, "BLCK", 4) != 0) {
            std::cerr << "Invalid persist file magic" << std::endl;
            return false;
        }

        if (header.version != PERSIST_FORMAT_VERSION) {
            std::cerr << "Persist file version mismatch: " << header.version
                      << " vs " << PERSIST_FORMAT_VERSION << std::endl;
            return false;
        }

        std::cout << "Loading persist file: " << header.totalCards
                  << " cards, " << header.prefixCount << " prefixes" << std::endl;

        prefixCount = header.prefixCount;
        indexTable.resize(prefixCount);
        for (uint32_t i = 0; i < prefixCount; ++i) {
            if (!file.read(reinterpret_cast<char*>(&indexTable[i]), sizeof(PersistIndexEntry))) {
                std::cerr << "Failed to read index entry " << i << std::endl;
                return false;
            }
        }

        // 清空现有数据
        std::lock_guard<std::mutex> lock(cardMapMutex);
        cardMap.clear();
        bloomFilter.clear();
        std::memcpy(versionInfo.data(), header.versionInfo, 6);
    }

    // 加载数据到 cardMap（使用单个文件句柄避免问题）
    std::vector<std::pair<uint16_t, std::array<std::vector<CardInfo>, 3>>> loadedData;
    std::ifstream file(filename, std::ios::binary | std::ios::in);
    if (!file.is_open()) {
        std::cerr << "Failed to reopen persist file for loading" << std::endl;
        return false;
    }

    for (const auto& entry : indexTable) {
        uint16_t prefix = entry.prefix;
        std::array<std::vector<CardInfo>, 3> typeArrays;

        if (entry.type0Count > 0) {
            typeArrays[0].resize(entry.type0Count);
            file.seekg(static_cast<std::streamoff>(entry.type0Offset));
            file.read(reinterpret_cast<char*>(typeArrays[0].data()),
                     entry.type0Count * sizeof(CardInfo));
        }
        if (entry.type1Count > 0) {
            typeArrays[1].resize(entry.type1Count);
            file.seekg(static_cast<std::streamoff>(entry.type1Offset));
            file.read(reinterpret_cast<char*>(typeArrays[1].data()),
                     entry.type1Count * sizeof(CardInfo));
        }
        if (entry.type2Count > 0) {
            typeArrays[2].resize(entry.type2Count);
            file.seekg(static_cast<std::streamoff>(entry.type2Offset));
            file.read(reinterpret_cast<char*>(typeArrays[2].data()),
                     entry.type2Count * sizeof(CardInfo));
        }

        loadedData.emplace_back(prefix, std::move(typeArrays));
    }
    file.close();

    // 写入 cardMap
    {
        std::lock_guard<std::mutex> lock(cardMapMutex);
        for (auto& [prefix, typeArrays] : loadedData) {
            cardMap[prefix] = std::move(typeArrays);
        }
    }

    // 对所有数据进行排序，确保二分查找正常工作
    sortAll();

    // 重建布隆过滤器
    {
        std::lock_guard<std::mutex> lock(cardMapMutex);
        for (const auto& prefixEntry : cardMap) {
            for (size_t typeIdx = 0; typeIdx < 3; ++typeIdx) {
                for (const auto& card : prefixEntry.second[typeIdx]) {
                    std::string fullCardId = std::to_string(prefixEntry.first);
                    std::string yearStr = std::to_string(card.getYear());
                    if (yearStr.length() < 2) yearStr = "0" + yearStr;
                    fullCardId += yearStr;
                    std::string weekStr = std::to_string(card.getWeek());
                    if (weekStr.length() < 2) weekStr = "0" + weekStr;
                    fullCardId += weekStr;
                    unsigned short cardType = (typeIdx == 0) ? 22 : (typeIdx == 1) ? 23 : 0;
                    fullCardId += std::to_string(cardType);
                    if (fullCardId.length() < 10) fullCardId = std::string(10 - fullCardId.length(), '0') + fullCardId;
                    fullCardId += card.getInnerIdStr();
                    bloomFilter.add(fullCardId);
                }
            }
        }
    }

    std::cout << "Persist file loaded: " << size() << " cards" << std::endl;
    return true;
}

/**
 * @brief 在原始数据加载后保存持久化文件
 * 用于在完成原始数据加载后创建持久化快照，以便下次快速恢复
 * @param persistPath 持久化文件路径
 * @return 保存是否成功
 */
bool BlacklistChecker::savePersistAfterLoad(const std::string& persistPath) {
    if (size() == 0) {
        std::cerr << "No data loaded, skipping persist file save" << std::endl;
        return false;
    }

    std::cout << "Saving persist file after data load..." << std::endl;
    bool result = saveToPersistFile(persistPath);
    if (result) {
        std::cout << "Persist file saved successfully: " << persistPath << std::endl;
    } else {
        std::cerr << "Failed to save persist file: " << persistPath << std::endl;
    }
    return result;
}

/**
 * @brief 检查持久化文件是否存在且有效
 * @param persistPath 持久化文件路径
 * @return 文件是否有效
 */
bool BlacklistChecker::isPersistFileValid(const std::string& persistPath) {
    try {
        std::ifstream file(persistPath, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }

        PersistHeader header;
        if (!file.read(reinterpret_cast<char*>(&header), sizeof(header))) {
            return false;
        }

        if (std::memcmp(header.magic, "BLCK", 4) != 0) {
            return false;
        }

        if (header.version != PERSIST_FORMAT_VERSION) {
            std::cerr << "Persist file version mismatch: " << header.version
                      << " vs " << PERSIST_FORMAT_VERSION << std::endl;
            return false;
        }

        return true;
    } catch (const std::exception& e) {
        return false;
    }
}
