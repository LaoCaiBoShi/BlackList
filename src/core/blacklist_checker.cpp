#define _CRT_SECURE_NO_WARNINGS

#include "blacklist_checker.h"
#include "log_manager.h"
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
BlacklistChecker::BlacklistChecker(QueryMode mode) : queryMode_(mode) {
    std::fill(versionInfo.begin(), versionInfo.end(), ' ');

    const char* modeName;
    switch (mode) {
        case QueryMode::BLOOM_ONLY:
            modeName = "BLOOM_ONLY";
            break;
        case QueryMode::CARDINFO_ONLY:
            modeName = "CARDINFO_ONLY";
            break;
        case QueryMode::BLOOM_AND_CARDINFO:
        default:
            modeName = "BLOOM_AND_CARDINFO";
            break;
    }

    std::cout << "[BlacklistChecker] Created with mode: " << modeName
              << " (default: HIGH precision, 10⁻⁶)" << std::endl;
}

void BlacklistChecker::setQueryMode(QueryMode mode) {
    if (size() > 0) {
        std::cerr << "[ERROR] Cannot change query mode after data loaded" << std::endl;
        return;
    }
    queryMode_ = mode;
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
 * @brief 加载黑名单（已废弃，请使用loadFromJsonFile）
 * @param filename 文件名
 * @return 加载是否成功
 */
bool BlacklistChecker::loadFromFile(const std::string& filename) {
    std::cerr << "loadFromFile is deprecated, use loadFromJsonFile instead" << std::endl;
    return false;
}

/**
 * @brief 定期更新黑名单（已废弃，请使用loadFromJsonFile）
 * @param filename 文件名
 * @return 更新是否成功
 */
bool BlacklistChecker::updateFromFile(const std::string& filename) {
    std::cerr << "updateFromFile is deprecated, use loadFromJsonFile instead" << std::endl;
    return false;
}

/**
 * @brief 添加到黑名单
 * @param cardId 卡片号
 */
void BlacklistChecker::add(const std::string& cardId) {
    if (cardId.length() != 20) return;

    unsigned short provinceCode = getProvinceCode(cardId);
    unsigned short type = getCardType(cardId);
    unsigned short year = getYear(cardId);
    unsigned short week = getWeek(cardId);
    std::string innerId = getInnerId(cardId);

    CardInfo cardInfo(year, week, innerId);

    // 根据模式决定存储策略
    switch (queryMode_) {
        case QueryMode::BLOOM_ONLY:
            // 只添加布隆过滤器
            bloomFilter.add(cardId);
            break;

        case QueryMode::CARDINFO_ONLY:
            // 只添加CardInfo存储
            {
                size_t shardIdx = getShardIndex(provinceCode);
                std::lock_guard<std::mutex> lock(provinceShards[shardIdx].mutex);
                provinceShards[shardIdx].cards[getTypeIndex(type)].push_back(cardInfo);
            }
            break;

        case QueryMode::BLOOM_AND_CARDINFO:
        default:
            // 添加到两者
            {
                size_t shardIdx = getShardIndex(provinceCode);
                std::lock_guard<std::mutex> lock(provinceShards[shardIdx].mutex);
                provinceShards[shardIdx].cards[getTypeIndex(type)].push_back(cardInfo);
            }
            bloomFilter.add(cardId);
            break;
    }
}

/**
 * @brief 批量添加到黑名单（按省份分片并行）
 * @param cardIds 卡号列表
 */
void BlacklistChecker::addBatch(const std::vector<std::string>& cardIds) {
    std::unordered_map<size_t, std::vector<std::string>> localByShard;
    std::vector<std::string> localCardIds;
    localCardIds.reserve(cardIds.size());

    for (const std::string& cardId : cardIds) {
        if (cardId.length() != 20) continue;

        unsigned short provinceCode = getProvinceCode(cardId);
        size_t shardIdx = getShardIndex(provinceCode);
        localByShard[shardIdx].push_back(cardId);
        localCardIds.push_back(cardId);
    }

    std::vector<std::thread> threads;
    threads.reserve(localByShard.size());

    for (auto& [shardIdx, shardCards] : localByShard) {
        threads.emplace_back([this, shardIdx, &shardCards]() {
            std::lock_guard<std::mutex> lock(provinceShards[shardIdx].mutex);
            for (const std::string& cardId : shardCards) {
                unsigned short type = getCardType(cardId);
                unsigned short year = getYear(cardId);
                unsigned short week = getWeek(cardId);
                std::string innerId = getInnerId(cardId);
                CardInfo cardInfo(year, week, innerId);
                provinceShards[shardIdx].cards[getTypeIndex(type)].push_back(cardInfo);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

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

    unsigned short provinceCode = getProvinceCode(cardId);
    unsigned short type = getCardType(cardId);
    unsigned short year = getYear(cardId);
    unsigned short week = getWeek(cardId);
    std::string innerId = getInnerId(cardId);

    CardInfo cardInfo(year, week, innerId);

    size_t shardIdx = getShardIndex(provinceCode);
    std::lock_guard<std::mutex> lock(provinceShards[shardIdx].mutex);
    auto& cards = provinceShards[shardIdx].cards[getTypeIndex(type)];
    auto cardIt = std::find(cards.begin(), cards.end(), cardInfo);
    if (cardIt != cards.end()) {
        cards.erase(cardIt);
    }
}

/**
 * @brief 检查是否在黑名单
 * @param cardId 卡片号
 * @return 是否在黑名单中
 */
bool BlacklistChecker::isBlacklisted(const std::string& cardId) {
    return checkCard(cardId).isBlacklisted;
}

/**
 * @brief 检查是否在黑名单并返回详细结果
 * @param cardId 卡片号
 * @return 查询结果结构体
 */
QueryResult BlacklistChecker::checkCard(const std::string& cardId) {
    if (cardId.length() != 20) {
        return QueryResult(false, QueryMethod::BLOOM_FAST_REJECT);
    }

    LOG_DEBUG("checkCard: %.4s****, length=%zu", cardId.c_str(), cardId.length());

    unsigned short provinceCode = getProvinceCode(cardId);
    unsigned short type = getCardType(cardId);
    unsigned short year = getYear(cardId);
    unsigned short week = getWeek(cardId);
    std::string innerId = getInnerId(cardId);
    size_t typeIdx = getTypeIndex(type);

    switch (queryMode_) {
        case QueryMode::BLOOM_ONLY:
            {
                bool contained = bloomFilter.contains(cardId);
                return QueryResult(contained, QueryMethod::BLOOM_CONTAINED);
            }

        case QueryMode::CARDINFO_ONLY:
            {
                CardInfo cardInfo(year, week, innerId);
                size_t shardIdx = getShardIndex(provinceCode);
                std::lock_guard<std::mutex> lock(provinceShards[shardIdx].mutex);
                const auto& cards = provinceShards[shardIdx].cards[typeIdx];
                bool found = std::binary_search(cards.begin(), cards.end(), cardInfo);
                return QueryResult(found, QueryMethod::CARDINFO_EXACT);
            }

        case QueryMode::BLOOM_AND_CARDINFO:
        default:
            {
                if (!bloomFilter.contains(cardId)) {
                    LOG_DEBUG("checkCard: %.4s**** - Bloom filter: NOT IN BLOOM (fast reject)", cardId.c_str());
                    return QueryResult(false, QueryMethod::BLOOM_FAST_REJECT);
                }
                LOG_DEBUG("checkCard: %.4s**** - Bloom filter: IN BLOOM (need precise check)", cardId.c_str());

                CardInfo cardInfo(year, week, innerId);

                size_t shardIdx = getShardIndex(provinceCode);
                LOG_DEBUG("checkCard: %.4s**** - province=%u, type=%u, year=%u, week=%u, typeIdx=%zu, shardIdx=%zu",
                          cardId.c_str(), provinceCode, type, year, week, typeIdx, shardIdx);

                std::lock_guard<std::mutex> lock(provinceShards[shardIdx].mutex);

                const auto& cards = provinceShards[shardIdx].cards[typeIdx];
                bool found = std::binary_search(cards.begin(), cards.end(), cardInfo);

                LOG_DEBUG("checkCard: %.4s**** - binary_search result: %s, cards in shard: %zu",
                          cardId.c_str(), found ? "FOUND" : "NOT FOUND", cards.size());

                return QueryResult(found, QueryMethod::CARDINFO_EXACT);
            }
    }
}

/**
 * @brief 获取黑名单大小
 * @return 黑名单大小
 */
size_t BlacklistChecker::size() const {
    size_t count = 0;
    for (size_t shardIdx = 0; shardIdx < MAX_PROVINCE_CODE; ++shardIdx) {
        for (size_t typeIdx = 0; typeIdx < 3; ++typeIdx) {
            count += provinceShards[shardIdx].cards[typeIdx].size();
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

        for (char c : versionInfo) {
            file << c;
        }
        file << '\n';

        for (size_t shardIdx = 0; shardIdx < MAX_PROVINCE_CODE; ++shardIdx) {
            for (size_t typeIdx = 0; typeIdx < 3; ++typeIdx) {
                for (const auto& card : provinceShards[shardIdx].cards[typeIdx]) {
                    std::string fullCardId = std::to_string(shardIdx);
                    std::string yearStr = std::to_string(card.getYear());
                    if (yearStr.length() < 2) yearStr = "0" + yearStr;
                    fullCardId += yearStr;
                    std::string weekStr = std::to_string(card.getWeek());
                    if (weekStr.length() < 2) weekStr = "0" + weekStr;
                    fullCardId += weekStr;
                    unsigned short cardType = (typeIdx == 0) ? 22 : (typeIdx == 1) ? 23 : 0;
                    std::string typeStr = std::to_string(cardType);
                    if (typeStr.length() < 2) typeStr = "0" + typeStr;
                    fullCardId += typeStr;
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
 * @param version 版本信息（应为空格或8字符的YYYYMMDD格式）
 */
void BlacklistChecker::setVersionInfo(const std::string& version) {
    std::fill(versionInfo.begin(), versionInfo.end(), ' ');
    for (size_t i = 0; i < 8 && i < version.length(); ++i) {
        versionInfo[i] = version[i];
    }
}

/**
 * @brief 获取版本信息
 * @return 版本信息（去除尾部空格）
 */
std::string BlacklistChecker::getVersionInfo() const {
    std::string result(versionInfo.begin(), versionInfo.end());
    size_t lastNonSpace = result.find_last_not_of(' ');
    if (lastNonSpace != std::string::npos) {
        result = result.substr(0, lastNonSpace + 1);
    }
    return result;
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
// 线程局部存储，复用parser避免每次创建的开销
thread_local simdjson::dom::parser localParser;

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

        // 使用 simdjson 解析 JSON（复用线程局部的parser）
        auto json_result = localParser.parse(content);

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

            // 直接使用 string_view 验证，避免不必要的字符串复制
            if (cardIdView.length() == 20) {
                // 验证卡号格式（纯数字）
                bool valid = true;
                for (char c : cardIdView) {
                    if (c < '0' || c > '9') {
                        valid = false;
                        break;
                    }
                }

                if (valid) {
                    validCardIds.emplace_back(cardIdView); // 使用 string_view 构造 string
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
    size_t totalExpectedCards = additionalRecords;
    size_t avgCardsPerProvince = totalExpectedCards / MAX_PROVINCE_CODE;
    size_t avgCardsPerType = avgCardsPerProvince / 3;

    for (size_t shardIdx = 0; shardIdx < MAX_PROVINCE_CODE; ++shardIdx) {
        for (size_t typeIdx = 0; typeIdx < 3; ++typeIdx) {
            provinceShards[shardIdx].cards[typeIdx].reserve(avgCardsPerType);
        }
    }

    std::cout << "Reserved capacity for " << additionalRecords << " additional records" << std::endl;
}

bool BlacklistChecker::reserveProvinceCapacitySafe(int provinceCode,
                                                    size_t jsonFileCount,
                                                    size_t cardsPerJson,
                                                    double bufferFactor) {
    if (provinceCode < 0 || static_cast<size_t>(provinceCode) >= MAX_PROVINCE_CODE) {
        std::cerr << "[ERROR] Invalid province code: " << provinceCode
                  << " (valid range: 0-" << (MAX_PROVINCE_CODE - 1) << ")" << std::endl;
        return false;
    }
    
    if (jsonFileCount == 0) {
        std::cout << "[WARN] Province " << provinceCode << " has 0 JSON files, skipping pre-allocation" << std::endl;
        return true;
    }
    
    double estimated = static_cast<double>(jsonFileCount) * 
                       static_cast<double>(cardsPerJson) * 
                       bufferFactor;
    
    const size_t MAX_REASONABLE_CARDS = 10000000;
    if (estimated > MAX_REASONABLE_CARDS) {
        std::cerr << "[WARN] Province " << provinceCode << " estimated " 
                  << estimated << " cards exceeds limit, capping" << std::endl;
        estimated = MAX_REASONABLE_CARDS;
    }
    
    size_t estimatedTotalCards = static_cast<size_t>(estimated);
    size_t perTypeCapacity = estimatedTotalCards / 3;
    const size_t MIN_CAPACITY_PER_TYPE = 100;
    
    if (perTypeCapacity < MIN_CAPACITY_PER_TYPE) {
        perTypeCapacity = MIN_CAPACITY_PER_TYPE;
    }
    
    try {
        size_t shardIdx = getShardIndex(static_cast<unsigned short>(provinceCode));
        std::lock_guard<std::mutex> lock(provinceShards[shardIdx].mutex);
        
        for (size_t typeIdx = 0; typeIdx < 3; ++typeIdx) {
            auto& vec = provinceShards[shardIdx].cards[typeIdx];
            if (vec.capacity() < perTypeCapacity) {
                vec.reserve(perTypeCapacity);
            }
        }
        
        LOG_DEBUG("Reserved capacity for province %d: %zu total cards (%zu per type)", 
                  provinceCode, estimatedTotalCards, perTypeCapacity);
        
    } catch (const std::bad_alloc& e) {
        std::cerr << "[ERROR] Memory allocation failed for province " << provinceCode
                  << ": " << e.what() << std::endl;
        std::cerr << "[ERROR] Requested capacity: " << perTypeCapacity << " per type" << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Unexpected error in reserveProvinceCapacitySafe: "
                  << e.what() << std::endl;
        return false;
    }

    return true;
}

/**
 * @brief 对所有卡片进行排序（省份分片并行排序）
 */
void BlacklistChecker::sortAll() {
    size_t threadCount = std::thread::hardware_concurrency();
    if (threadCount == 0) threadCount = 4;

    std::vector<std::thread> threads;
    threads.reserve(threadCount);

    size_t shardsPerThread = std::max<size_t>(1, MAX_PROVINCE_CODE / threadCount);

    for (size_t i = 0; i < MAX_PROVINCE_CODE; i += shardsPerThread) {
        size_t endIdx = std::min(i + shardsPerThread, MAX_PROVINCE_CODE);

        threads.emplace_back([this, i, endIdx]() {
            for (size_t shardIdx = i; shardIdx < endIdx; ++shardIdx) {
                std::lock_guard<std::mutex> lock(provinceShards[shardIdx].mutex);
                for (size_t typeIdx = 0; typeIdx < 3; ++typeIdx) {
                    auto& cards = provinceShards[shardIdx].cards[typeIdx];
                    if (cards.size() > 1) {
                        std::sort(cards.begin(), cards.end());
                    }
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }
}

void BlacklistChecker::sortProvince(int provinceCode) {
    if (provinceCode < 0 || static_cast<size_t>(provinceCode) >= MAX_PROVINCE_CODE) {
        LOG_WARN("sortProvince: invalid province code %d", provinceCode);
        return;
    }

    LOG_DEBUG("sortProvince %d: starting", provinceCode);
    for (size_t typeIdx = 0; typeIdx < 3; ++typeIdx) {
        auto& cards = provinceShards[provinceCode].cards[typeIdx];
        if (cards.size() > 1) {
            std::sort(cards.begin(), cards.end());
        }
    }
    LOG_DEBUG("sortProvince %d: completed", provinceCode);
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
    size_t size = 0;

    for (size_t shardIdx = 0; shardIdx < MAX_PROVINCE_CODE; ++shardIdx) {
        for (size_t typeIdx = 0; typeIdx < 3; ++typeIdx) {
            size += provinceShards[shardIdx].cards[typeIdx].size() * sizeof(CardInfo);
        }
    }

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
        std::cout << "[BlacklistChecker] Opening file for writing: " << filename << std::endl;
        LOG_INFO("Opening persist file for writing: %s", filename.c_str());

        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "[BlacklistChecker] Failed to open persist file for writing: " << filename << std::endl;
            LOG_ERROR("Failed to open persist file for writing: %s", filename.c_str());
            return false;
        }
        std::cout << "[BlacklistChecker] File opened successfully" << std::endl;
        LOG_DEBUG("Persist file opened successfully");

        const size_t headerSize = sizeof(PersistHeader);
        const size_t indexEntrySize = sizeof(PersistIndexEntry);

        std::vector<std::pair<uint16_t, std::array<uint64_t, 3>>> prefixOffsets;
        std::vector<std::pair<uint16_t, std::array<uint32_t, 3>>> prefixCounts;
        size_t currentOffset = headerSize;

        size_t totalCards = 0;
        for (size_t shardIdx = 0; shardIdx < MAX_PROVINCE_CODE; ++shardIdx) {
            uint16_t provinceCode = static_cast<uint16_t>(shardIdx);
            std::array<uint32_t, 3> counts = {
                static_cast<uint32_t>(provinceShards[shardIdx].cards[0].size()),
                static_cast<uint32_t>(provinceShards[shardIdx].cards[1].size()),
                static_cast<uint32_t>(provinceShards[shardIdx].cards[2].size())
            };

            if (counts[0] == 0 && counts[1] == 0 && counts[2] == 0) {
                continue;
            }

            totalCards += counts[0] + counts[1] + counts[2];

            std::array<uint64_t, 3> offsets = {0, 0, 0};
            offsets[0] = currentOffset;
            currentOffset += static_cast<uint64_t>(counts[0]) * sizeof(CardInfo);
            offsets[1] = currentOffset;
            currentOffset += static_cast<uint64_t>(counts[1]) * sizeof(CardInfo);
            offsets[2] = currentOffset;
            currentOffset += static_cast<uint64_t>(counts[2]) * sizeof(CardInfo);
            prefixOffsets.push_back({provinceCode, offsets});
            prefixCounts.push_back({provinceCode, counts});
        }

        size_t prefixCount = prefixOffsets.size();
        size_t indexTableSize = prefixCount * indexEntrySize;
        size_t dataStartOffset = headerSize + indexTableSize;

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

        PersistHeader header;
        std::memset(&header, 0, sizeof(header));
        std::memcpy(header.magic, "BLCK", 4);
        header.version = PERSIST_FORMAT_VERSION;
        header.prefixCount = static_cast<uint32_t>(prefixCount);
        header.totalCards = static_cast<uint32_t>(size());
        header.bloomFilterBits = bloomFilter.getTotalBits();
        header.createdTime = static_cast<uint64_t>(std::time(nullptr));
        std::memcpy(header.versionInfo, versionInfo.data(), 8);
        file.write(reinterpret_cast<const char*>(&header), sizeof(header));

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

        for (size_t i = 0; i < prefixOffsets.size(); ++i) {
            uint16_t provinceCode = prefixOffsets[i].first;
            size_t shardIdx = getShardIndex(provinceCode);
            for (size_t typeIdx = 0; typeIdx < 3; ++typeIdx) {
                const auto& cards = provinceShards[shardIdx].cards[typeIdx];
                if (!cards.empty()) {
                    file.write(reinterpret_cast<const char*>(cards.data()),
                              cards.size() * sizeof(CardInfo));
                }
            }
        }

        file.close();
        std::cout << "[BlacklistChecker] Persist file saved: " << filename << std::endl;
        std::cout << "[BlacklistChecker] Saved " << totalCards << " cards in "
                  << prefixOffsets.size() << " provinces" << std::endl;
        LOG_INFO("Persist file saved: %s, cards: %zu, provinces: %zu",
                 filename.c_str(), totalCards, prefixOffsets.size());
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[BlacklistChecker] Error saving persist file: " << e.what() << std::endl;
        LOG_ERROR("Error saving persist file: %s", e.what());
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

        bloomFilter.clear();
        std::memcpy(versionInfo.data(), header.versionInfo, 8);
    }

    std::vector<std::pair<uint16_t, std::array<std::vector<CardInfo>, 3>>> loadedData;
    std::ifstream file(filename, std::ios::binary | std::ios::in);
    if (!file.is_open()) {
        std::cerr << "Failed to reopen persist file for loading" << std::endl;
        return false;
    }

    for (const auto& entry : indexTable) {
        uint16_t provinceCode = entry.prefix;
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

        loadedData.emplace_back(provinceCode, std::move(typeArrays));
    }
    file.close();

    for (auto& [provinceCode, typeArrays] : loadedData) {
        size_t shardIdx = getShardIndex(provinceCode);
        std::lock_guard<std::mutex> lock(provinceShards[shardIdx].mutex);
        provinceShards[shardIdx].cards = std::move(typeArrays);
    }

    sortAll();

    for (size_t shardIdx = 0; shardIdx < MAX_PROVINCE_CODE; ++shardIdx) {
        for (size_t typeIdx = 0; typeIdx < 3; ++typeIdx) {
            for (const auto& card : provinceShards[shardIdx].cards[typeIdx]) {
                std::string fullCardId = std::to_string(shardIdx);
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
