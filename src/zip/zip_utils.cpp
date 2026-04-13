#include "zip_utils.h"
#include "zip_basic.h"
#include "blacklist_checker.h"
#include "ZipExtractor.h"
#include "thread_pool.h"
#include "system_utils.h"
#include "memory_pool.h"
#include "log_manager.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <functional>
#include <unordered_map>

// simdjson JSON 解析库
#include "simdjson.h"
#include "memory_pool.h"

// Windows 特定头文件
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

constexpr size_t MAX_SINGLE_FILE_SIZE = 50 * 1024 * 1024;  // 单个JSON文件最大50MB
constexpr size_t MAX_JSON_FILES_COUNT = 100000;              // 最大JSON文件数量
constexpr int MAX_RETRY_COUNT = 3;                          // 最大重试次数
constexpr int TERMINATION_SIGNAL_PROVINCE = -1;              // 终止信号专用省份代码

// 全局计数器
std::atomic<size_t> globalLoadedCount(0);

/**
 * @brief 获取动态计算的消费者线程数
 * @param cpuCount CPU核心数
 * @param provinceCount 省份数量
 * @return 推荐的每省消费者线程数
 *
 * 计算策略：
 * - 8核CPU: 每省2个消费者 → 最大16省份并行 = 32线程
 * - 16核CPU: 每省2个消费者 → 最大8省份并行 = 16线程
 * - 4核CPU: 每省2个消费者 → 最大4省份并行 = 8线程
 */
size_t calculateConsumerThreads(size_t cpuCount, size_t provinceCount) {
    if (cpuCount == 0) cpuCount = 4;

    // 公式: 每省消费者数 = max(2, cpuCount / 省份数)
    // 这样可以确保总线程数不会过多，同时保持较好的并行性
    size_t consumersPerProvince = std::max<size_t>(2, cpuCount / std::max<size_t>(provinceCount, 1));
    consumersPerProvince = std::min<size_t>(consumersPerProvince, 4);  // 最多4个

    return consumersPerProvince;
}

/**
 * @brief 获取动态计算的省份并行数
 * @param cpuCount CPU核心数
 * @param provinceCount 省份数量
 * @param consumersPerProvince 每省消费者数
 * @return 推荐的省份并行数
 */
size_t calculateParallelProvinces(size_t cpuCount, size_t provinceCount, size_t consumersPerProvince) {
    if (cpuCount == 0) cpuCount = 4;

    // 公式: 省份并行数 = cpuCount / 每省消费者数
    // 这样可以确保总线程数 = cpuCount，不会过多
    size_t parallelProvinces = std::max<size_t>(1, cpuCount / consumersPerProvince);
    parallelProvinces = std::min<size_t>(parallelProvinces, provinceCount);

    return parallelProvinces;
}

/**
 * @brief 线程安全队列，用于存储 JSON 文件路径
 *        带超时机制的消费者-生产者模式
 */
template <typename T>
class ThreadSafeQueue {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration = std::chrono::milliseconds;

    /**
     * @brief 构造函数
     * @param maxSize 队列最大容量，0表示无限制
     */
    ThreadSafeQueue(size_t maxSize = 0) : done(false), maxSize(maxSize) {}

    /**
     * @brief 向队列中添加元素（带超时）
     * @param item 要添加的元素
     * @param timeout 超时时间
     * @return 是否添加成功
     */
    bool push(T item, const Duration& timeout = Duration(1000)) {
        std::unique_lock<std::mutex> lock(mutex);
        if (maxSize > 0) {
            if (!notFull.wait_for(lock, timeout, [this]() {
                return queue.size() < maxSize || done;
            })) {
                std::cerr << "[ThreadSafeQueue] Push timeout" << std::endl;
                return false;
            }
        }
        if (done) {
            std::cerr << "[ThreadSafeQueue] Push rejected: queue is done" << std::endl;
            return false;
        }
        queue.push(std::move(item));
        notEmpty.notify_one();
        return true;
    }

    /**
     * @brief 从队列中获取元素（带超时）
     * @param timeout 超时时间
     * @return 队列中的元素，如果队列为空或超时，返回默认构造的元素
     */
    T pop(const Duration& timeout = Duration(1000)) {
        std::unique_lock<std::mutex> lock(mutex);
        if (!notEmpty.wait_for(lock, timeout, [this]() {
            return !queue.empty() || done;
        })) {
            return T();
        }
        if (queue.empty()) {
            return T();
        }
        T item = std::move(queue.front());
        queue.pop();
        if (maxSize > 0) {
            notFull.notify_one();
        }
        return item;
    }

    /**
     * @brief 尝试弹出元素（非阻塞）
     * @param item 输出参数，弹出的元素
     * @return 是否成功弹出
     */
    bool try_pop(T& item) {
        std::lock_guard<std::mutex> lock(mutex);
        if (queue.empty()) {
            return false;
        }
        item = std::move(queue.front());
        queue.pop();
        if (maxSize > 0) {
            notFull.notify_one();
        }
        return true;
    }

    /**
     * @brief 标记队列完成
     */
    void setDone() {
        std::lock_guard<std::mutex> lock(mutex);
        done = true;
        notEmpty.notify_all();
        notFull.notify_all();
    }

    /**
     * @brief 检查队列是否为空
     * @return 队列是否为空
     */
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex);
        return queue.empty();
    }

    /**
     * @brief 获取队列当前大小
     * @return 队列大小
     */
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex);
        return queue.size();
    }

    /**
     * @brief 获取队列容量
     * @return 队列容量
     */
    size_t capacity() const {
        return maxSize;
    }

private:
    mutable std::queue<T> queue;       // 存储元素的队列
    mutable std::mutex mutex;         // 互斥锁
    std::condition_variable notEmpty;  // 非空条件变量
    std::condition_variable notFull;   // 非满条件变量
    bool done;                        // 完成标记
    size_t maxSize;                   // 队列最大容量
};

/**
 * @brief 压缩文件信息
 */
struct ZipFileInfo {
    std::string path;   // 文件路径
    uint64_t size;      // 文件大小
};

/**
 * @brief 线程安全的计数器
 */
struct ThreadSafeCounter {
    std::atomic<size_t> loaded = 0;     // 成功加载的数量
    std::atomic<size_t> invalid = 0;    // 无效卡片ID数量

    /**
     * @brief 添加成功加载的数量
     * @param count 数量
     */
    void addLoaded(size_t count) { loaded += count; }

    /**
     * @brief 添加无效卡片ID数量
     * @param count 数量
     */
    void addInvalid(size_t count) { invalid += count; }
};

/**
 * @brief JSON任务结构体，用于内存化解压处理
 */
struct JsonTask {
    int provinceCode;           // 省份代码
    std::string content;        // JSON文件内容（内存中）
    std::string filePath;       // 原始文件路径（用于日志）

    JsonTask() : provinceCode(0) {}

    JsonTask(int code, std::string&& jsonContent, const std::string& path)
        : provinceCode(code), content(std::move(jsonContent)), filePath(path) {}

    static JsonTask createTerminationTask() {
        return JsonTask(TERMINATION_SIGNAL_PROVINCE, std::string(), std::string());
    }

    bool isTermination() const {
        return provinceCode == TERMINATION_SIGNAL_PROVINCE;
    }

    bool isEmpty() const {
        return provinceCode == 0 && content.empty();
    }
};

/**
 * @brief 获取文件大小
 * @param filePath 文件路径
 * @return 文件大小
 */
uint64_t getFileSize(const std::string& filePath) {
    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    if (GetFileAttributesExA(filePath.c_str(), GetFileExInfoStandard, &fileInfo)) {
        LARGE_INTEGER size;
        size.HighPart = fileInfo.nFileSizeHigh;
        size.LowPart = fileInfo.nFileSizeLow;
        return size.QuadPart;
    }
    return 0;
}

/**
 * @brief 从 JSON 文件中提取所有卡号
 * @param filePath 文件路径
 * @param cardIds 输出：提取的卡号列表
 * @return 是否成功
 */
bool extractCardIdsFromJson(const std::string& filePath, std::vector<std::string>& cardIds) {
    try {
        // 检查是否是JSON文件
        std::string fileName = filePath.substr(filePath.find_last_of("\\/") + 1);
        if (fileName.length() <= 5 || fileName.substr(fileName.find_last_of(".") + 1) != "json") {
            std::cerr << "Not a JSON file: " << fileName << std::endl;
            return false;
        }

        // 打开文件
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Failed to open file: " << filePath << std::endl;
            return false;
        }

        // 预分配内存，减少扩容开销
        cardIds.reserve(10000); // 假设每个JSON文件平均包含10000个卡号

        // 一次性读取整个文件内容
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

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
        
        size_t foundCount = 0;
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
                    cardIds.emplace_back(std::string(cardIdView)); // 转换为string后添加
                    foundCount++;
                }
            }
        }
        
        std::cout << "Extracted " << foundCount << " card IDs from " << fileName << std::endl;
        return foundCount > 0;
    } catch (const std::exception& e) {
        std::cerr << "Error extracting card IDs from " << filePath << ": " << e.what() << std::endl;
        return false;
    }
}

/**
 * @brief 处理单个 JSON 文件（批量版本）
 * @param filePath 文件路径
 * @param checker BlacklistChecker实例
 * @param counter 线程安全计数器
 * @param batchSize 批量大小
 */
/**
 * @brief 验证字符串是否为有效的UTF-8编码
 * @param str 输入字符串
 * @param length 字符串长度
 * @return 是否为有效的UTF-8
 */
bool isValidUTF8(const char* str, size_t length) {
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(str);
    while (length > 0) {
        if (*bytes <= 0x7F) {
            bytes++;
            length--;
        } else if ((*bytes & 0xE0) == 0xC0) {
            if (length < 2) return false;
            if ((bytes[1] & 0xC0) != 0x80) return false;
            bytes += 2;
            length -= 2;
        } else if ((*bytes & 0xF0) == 0xE0) {
            if (length < 3) return false;
            if ((bytes[1] & 0xC0) != 0x80) return false;
            if ((bytes[2] & 0xC0) != 0x80) return false;
            bytes += 3;
            length -= 3;
        } else if ((*bytes & 0xF8) == 0xF0) {
            if (length < 4) return false;
            if ((bytes[1] & 0xC0) != 0x80) return false;
            if ((bytes[2] & 0xC0) != 0x80) return false;
            if ((bytes[3] & 0xC0) != 0x80) return false;
            bytes += 4;
            length -= 4;
        } else {
            return false;
        }
    }
    return true;
}

/**
 * @brief 从内存中的JSON内容提取卡号（带异常处理）
 * @param jsonContent JSON文件内容
 * @param filePath 文件路径（用于日志）
 * @param checker 黑名单检查器
 * @param counter 线程安全计数器
 * @return 处理是否成功
 */
bool processJsonContentFromMemory(const std::string& jsonContent, const std::string& filePath,
                                   BlacklistChecker& checker, ThreadSafeCounter& counter) {
    std::string fileName = filePath.substr(filePath.find_last_of("\\/") + 1);

    try {
        if (jsonContent.empty()) {
            std::cerr << "[JSON] Empty content: " << fileName << std::endl;
            return false;
        }

        size_t contentSize = jsonContent.size();
        if (contentSize > MAX_SINGLE_FILE_SIZE) {
            std::cerr << "[JSON] File too large: " << fileName
                      << " (" << contentSize << " bytes > " << MAX_SINGLE_FILE_SIZE << ")" << std::endl;
            return false;
        }

        if (!isValidUTF8(jsonContent.data(), jsonContent.size())) {
            std::cerr << "[JSON] Invalid UTF-8 encoding: " << fileName << std::endl;
            counter.addInvalid(1);
            return false;
        }

        thread_local simdjson::dom::parser localParser;
        auto json_result = localParser.parse(jsonContent);

        if (json_result.error()) {
            std::cerr << "[JSON] Parse error in " << fileName << ": "
                      << simdjson::error_message(json_result.error()) << std::endl;
            return false;
        }

        simdjson::dom::element json = json_result.value();

        if (!json.is_array()) {
            std::cerr << "[JSON] Root is not array: " << fileName << std::endl;
            return false;
        }

        simdjson::dom::array array = json.get_array();
        std::vector<std::string> validCardIds;
        validCardIds.reserve(std::min(array.size(), size_t(2000)));

        size_t invalidCount = 0;
        for (simdjson::dom::element element : array) {
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

            if (cardIdView.length() != 20) {
                invalidCount++;
                continue;
            }

            bool valid = true;
            for (char c : cardIdView) {
                if (c < '0' || c > '9') {
                    valid = false;
                    break;
                }
            }

            if (valid) {
                validCardIds.emplace_back(cardIdView);
            } else {
                invalidCount++;
            }
        }

        if (!validCardIds.empty()) {
            checker.addBatch(validCardIds);
            counter.addLoaded(validCardIds.size());
            globalLoadedCount += validCardIds.size();
        }

        if (invalidCount > 0) {
            counter.addInvalid(invalidCount);
        }

        return true;
    } catch (const std::bad_alloc& e) {
        std::cerr << "[JSON] Memory allocation failed for " << fileName
                  << ": " << e.what() << std::endl;
        return false;
    } catch (const simdjson::simdjson_error& e) {
        std::cerr << "[JSON] Simdjson error in " << fileName
                  << ": " << e.what() << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "[JSON] Unexpected error processing " << fileName
                  << ": " << e.what() << std::endl;
        return false;
    }
}

void processSingleJsonFileBatch(const std::string& filePath, BlacklistChecker& checker, ThreadSafeCounter& counter, size_t batchSize = 0) {
    // 如果未指定批处理大小，使用动态配置
    if (batchSize == 0) {
        ThreadConfig config = calculateThreadConfig();
        batchSize = config.batchSize;
    }
    try {
        std::string fileName = filePath.substr(filePath.find_last_of("\\/") + 1);
        
        // 检查文件是否存在
        std::ifstream testFile(filePath);
        if (!testFile.good()) {
            std::cerr << "File not accessible: " << filePath << std::endl;
            testFile.close();
            return;
        }
        testFile.close();
        
        std::cout << "Processing JSON file: " << fileName << std::endl;
        
        // 提取卡号
        size_t loaded = 0;
        size_t invalid = 0;
        if (checker.loadFromJsonFile(filePath, loaded, invalid)) {
            std::cout << "Extracted " << loaded << " card IDs from " << fileName << std::endl;
            counter.addLoaded(loaded);
            counter.addInvalid(invalid);
            // 更新全局计数器
            globalLoadedCount += loaded;
            // 每100000条显示当前的黑名单总数量
            if (globalLoadedCount % 100000 == 0 && globalLoadedCount > 0) {
                std::cout << "Current blacklist total: " << globalLoadedCount << " items" << std::endl;
            }
        } else {
            std::cerr << "Failed to extract card IDs from: " << fileName << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error processing file " << filePath << ": " << e.what() << std::endl;
    }
}

/**
 * @brief 省份压缩文件信息
 */
struct ProvinceZipInfo {
    int provinceCode;          // 省份代码
    std::string zipPath;       // 压缩文件路径
    uint64_t size;             // 文件大小
    std::vector<std::string> jsonFiles; // JSON文件列表（解压后）
};

// 前向声明
int extractProvinceCode(const std::string& fileName);

/**
 * @brief 解压压缩文件并收集JSON文件（实时处理版）
 * @param zipFilePath 压缩文件路径
 * @param jsonQueue JSON文件路径队列
 * @param destDir 目标目录
 */
void extractAndCollectJson(const std::string& zipFilePath, ThreadSafeQueue<std::string>& jsonQueue, const std::string& destDir) {
    try {
        std::cout << "Extracting file: " << zipFilePath << std::endl;
        
        // 创建 ZipExtractor 实例
        ZipExtractor extractor;
        
        // 打开压缩文件
        if (extractor.open(zipFilePath) != ZipExtractor::ZipResult::OK) {
            std::cerr << "Failed to open compressed file: " << extractor.getLastError() << std::endl;
            return;
        }
        
        // 提取压缩文件名（不含扩展名）作为目标目录名
        std::string zipFileName = zipFilePath.substr(zipFilePath.find_last_of("\\/") + 1);
        size_t dotPos = zipFileName.find_last_of(".");
        if (dotPos != std::string::npos) {
            zipFileName = zipFileName.substr(0, dotPos);
        }
        
        // 构建目标目录路径
        std::string extractDestDir = destDir + "\\" + zipFileName;
        
        // 解压所有文件，并在每个文件解压完成后检查是否是 JSON 文件
        int jsonCount = 0;
        auto fileCallback = [&](const std::string& filePath) {
            // 检查是否是 JSON 文件
            std::string fileName = filePath.substr(filePath.find_last_of("\\/") + 1);
            if (fileName.length() > 5 && fileName.substr(fileName.find_last_of(".") + 1) == "json") {
                // 使用实际解压后的路径
                std::string extractedPath = extractDestDir + "\\" + fileName;
                jsonQueue.push(extractedPath);
                jsonCount++;
                std::cout << "Added JSON file to queue: " << fileName << std::endl;
            }
        };
        
        // 解压所有文件（实时回调）
        if (extractor.extractAllWithRealTimeCallback(extractDestDir, fileCallback) != ZipExtractor::ZipResult::OK) {
            std::cerr << "Extraction failed: " << extractor.getLastError() << std::endl;
            return;
        }
        
        // 关闭压缩文件
        extractor.close();
        
        // 解压成功后删除zip文件以节省空间
        if (DeleteFileA(zipFilePath.c_str())) {
            std::cout << "Deleted zip file: " << zipFileName << std::endl;
        } else {
            std::cerr << "Failed to delete zip file: " << zipFileName << std::endl;
        }
        
        std::cout << "Found " << jsonCount << " JSON files in " << zipFilePath << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error extracting file " << zipFilePath << ": " << e.what() << std::endl;
    }
}

/**
 * @brief 从ZIP文件内容收集省份压缩文件信息（内存流式，不落盘）
 * @param zipFilePath ZIP文件路径
 * @return 省份压缩文件信息列表
 */
std::vector<ProvinceZipInfo> collectProvinceZipsFromZip(ZipExtractor& extractor, const std::string& zipFilePath) {
    std::vector<ProvinceZipInfo> provinceZips;

    std::cout << "  [Memory Stream] Scanning ZIP for province archives..." << std::endl;

    std::vector<std::string> allFiles = extractor.getFileList();
    int totalZips = 0;

    for (const auto& fileName : allFiles) {
        std::string shortName = fileName;
        size_t lastSlash = shortName.find_last_of("\\/");
        if (lastSlash != std::string::npos) {
            shortName = shortName.substr(lastSlash + 1);
        }

        if (shortName.find(".zip") != std::string::npos) {
            totalZips++;
            int provinceCode = extractProvinceCode(shortName);
            provinceZips.push_back({provinceCode, fileName, 0, {}});
            std::cout << "  Found province zip in ZIP: " << shortName
                      << " (province code: " << provinceCode << ")" << std::endl;
        }
    }

    std::cout << "  [Memory Stream] Total ZIP files found: " << totalZips << std::endl;

    std::sort(provinceZips.begin(), provinceZips.end(), [](const ProvinceZipInfo& a, const ProvinceZipInfo& b) -> bool {
        return a.size > b.size;
    });

    return provinceZips;
}

/**
 * @brief 收集解压目录中的省份压缩文件信息
 * @param directory 目录路径
 * @return 省份压缩文件信息列表
 */
std::vector<ProvinceZipInfo> collectProvinceZips(const std::string& directory) {
    std::vector<ProvinceZipInfo> provinceZips;
    
    // 调试：列出解压目录中的所有文件
    std::cout << "  [DEBUG] Scanning directory: " << directory << std::endl;
    int totalFiles = 0;
    int zipFiles = 0;
    
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA((directory + "\\*").c_str(), &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            std::string fileName = findData.cFileName;
            if (fileName == "." || fileName == "..") continue;
            
            totalFiles++;
            bool isZip = (fileName.find(".zip") != std::string::npos);
            if (isZip) zipFiles++;
            
            std::cout << "  [DEBUG] Found: " << fileName << " (size: " << findData.nFileSizeLow << ", isZip: " << isZip << ")" << std::endl;
        } while (FindNextFileA(hFind, &findData));
        FindClose(hFind);
    }
    std::cout << "  [DEBUG] Total files: " << totalFiles << ", ZIP files: " << zipFiles << std::endl;
    
    // 正式扫描 ZIP 文件
    hFind = FindFirstFileA((directory + "\\*.zip").c_str(), &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            std::string fileName = findData.cFileName;
            std::string filePath = directory + "\\" + fileName;
            int provinceCode = extractProvinceCode(fileName);
            uint64_t fileSize = getFileSize(filePath);
            
            provinceZips.push_back({provinceCode, filePath, fileSize, {}});
            std::cout << "Found province zip: " << fileName << " (province code: " << provinceCode << ", size: " << fileSize << ")" << std::endl;
        } while (FindNextFileA(hFind, &findData));
        FindClose(hFind);
    }
    
    // 按文件大小降序排序（优先处理大文件，减少后期等待）
    std::sort(provinceZips.begin(), provinceZips.end(), [](const ProvinceZipInfo& a, const ProvinceZipInfo& b) -> bool {
        return a.size > b.size;  // 大文件优先
    });
    
    // 输出优化后的处理顺序
    std::cout << "Province processing order (by size, largest first):" << std::endl;
    for (size_t i = 0; i < std::min(provinceZips.size(), (size_t)5); ++i) {
        std::cout << "  " << (i + 1) << ". Province " << provinceZips[i].provinceCode 
                  << " - " << provinceZips[i].size << " bytes" << std::endl;
    }
    if (provinceZips.size() > 5) {
        std::cout << "  ... and " << (provinceZips.size() - 5) << " more provinces" << std::endl;
    }
    
    return provinceZips;
}

/**
 * @brief 处理单个省份的压缩文件（真正的生产者-消费者模式）
 *        边解压边处理，不等待所有文件解压完成
 * @param provinceInfo 省份压缩文件信息
 * @param checker BlacklistChecker实例
 * @param counter 线程安全计数器
 */
void processSingleProvinceParallel(const ProvinceZipInfo& provinceInfo, BlacklistChecker& checker, ThreadSafeCounter& counter) {
    try {
        std::cout << "Processing province " << provinceInfo.provinceCode << " from " << provinceInfo.zipPath << std::endl;
        
        // 创建 ZipExtractor 实例
        ZipExtractor extractor;
        if (extractor.open(provinceInfo.zipPath) != ZipExtractor::ZipResult::OK) {
            std::cerr << "Failed to open " << provinceInfo.zipPath << ": " << extractor.getLastError() << std::endl;
            return;
        }
        
        // 提取压缩文件名作为目标目录名
        std::string zipFileName = provinceInfo.zipPath.substr(provinceInfo.zipPath.find_last_of("\\/") + 1);
        size_t dotPos = zipFileName.find_last_of(".");
        if (dotPos != std::string::npos) {
            zipFileName = zipFileName.substr(0, dotPos);
        }
        
        // 目标目录
        std::string extractDestDir = provinceInfo.zipPath.substr(0, provinceInfo.zipPath.find_last_of("\\/") + 1) + zipFileName;
        
        // 获取文件列表并统计JSON文件数量（用于预分配内存）
        std::vector<std::string> allFiles = extractor.getFileList();
        int jsonCount = 0;
        for (const auto& filePath : allFiles) {
            std::string fname = filePath.substr(filePath.find_last_of("\\/") + 1);
            if (fname.substr(fname.find_last_of(".") + 1) == "json") {
                jsonCount++;
            }
        }
        
        std::cout << "Province " << provinceInfo.provinceCode << " has " << jsonCount << " JSON files (Producer-Consumer mode)" << std::endl;
        
        // 根据 JSON 文件数量预分配内存（每个 JSON 文件约 1000 个卡号）
        const size_t CARD_IDS_PER_JSON = 1000;
        const double CAPACITY_BUFFER = 1.5;
        size_t estimatedTotalCards = static_cast<size_t>(jsonCount * CARD_IDS_PER_JSON * CAPACITY_BUFFER);
        std::cout << "Estimated total card IDs for province " << provinceInfo.provinceCode 
                  << ": " << estimatedTotalCards << std::endl;
        
        // 预分配黑名单容量
        checker.reserveCapacity(estimatedTotalCards);

        // ============================================================
        // 真正的生产者-消费者模式 - 边解压边入队
        // ============================================================

        ThreadSafeQueue<std::string> taskQueue;
        std::atomic<size_t> completedCount(0);
        std::atomic<size_t> producedCount(0);
        std::atomic<bool> extractionComplete(false);

        // 使用动态配置的线程数
        ThreadConfig config = calculateThreadConfig();
        size_t threadCount = config.parseThreads;
        std::cout << "Province " << provinceInfo.provinceCode << " using " << threadCount << " consumer threads" << std::endl;

        std::vector<std::thread> consumerThreads;
        consumerThreads.reserve(threadCount);

        for (size_t i = 0; i < threadCount; ++i) {
            consumerThreads.emplace_back([&]() {
                while (true) {
                    std::string task = taskQueue.pop();
                    if (task.empty()) {
                        // 收到空字符串，立即退出
                        break;
                    }

                    processSingleJsonFileBatch(task, checker, counter);
                    completedCount++;

                    if (completedCount % 100 == 0) {
                        std::cout << "Province " << provinceInfo.provinceCode << ": "
                                  << completedCount << " JSON files processed" << std::endl;
                    }
                }
            });
        }

        std::cout << "Province " << provinceInfo.provinceCode << ": Starting pipelined extraction and processing..." << std::endl;

        auto fileCallback = [&](const std::string& filePath) {
            std::string fileName = filePath.substr(filePath.find_last_of("\\/") + 1);
            size_t dotPos = fileName.find_last_of(".");
            if (dotPos != std::string::npos && fileName.substr(dotPos + 1) == "json") {
                std::string fullPath = extractDestDir + "\\" + fileName;
                taskQueue.push(fullPath);
                producedCount++;
            }
        };

        if (extractor.extractAllWithCallback(extractDestDir, fileCallback) != ZipExtractor::ZipResult::OK) {
            std::cerr << "Failed to extract " << provinceInfo.zipPath << ": " << extractor.getLastError() << std::endl;
            extractor.close();
            extractionComplete = true;
            for (size_t i = 0; i < threadCount; ++i) {
                taskQueue.push("");
            }
            for (auto& t : consumerThreads) {
                t.join();
            }
            return;
        }
        extractor.close();

        std::cout << "Province " << provinceInfo.provinceCode << ": Extracted " << producedCount
                  << " JSON files, waiting for consumers to finish..." << std::endl;

        extractionComplete = true;

        for (size_t i = 0; i < threadCount; ++i) {
            taskQueue.push("");
        }

        for (auto& t : consumerThreads) {
            t.join();
        }

        std::cout << "Province " << provinceInfo.provinceCode << " completed (consumed "
                  << completedCount << " JSON files)" << std::endl;

        if (DeleteFileA(provinceInfo.zipPath.c_str())) {
            std::cout << "Deleted province zip file: " << zipFileName << std::endl;
        }

        std::cout << "Province " << provinceInfo.provinceCode << " processing completed" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error processing province " << provinceInfo.provinceCode << ": " << e.what() << std::endl;
    }
}

/**
 * @brief 提取省份代码（二级压缩文件名如20230620_11的后两位）
 * @param fileName 文件名
 * @return 省份代码，如果提取失败返回0（默认省份）
 */
int extractProvinceCode(const std::string& fileName) {
    // 查找文件名中最后一个 '_' 字符
    // 例如：20230620_11.zip -> 提取 "11"
    size_t lastUnderscorePos = fileName.find_last_of('_');
    if (lastUnderscorePos == std::string::npos) {
        return 0;  // 没有 '_'，无法提取省份代码
    }
    
    // 获取 '_' 后面的部分
    std::string provinceStr = fileName.substr(lastUnderscorePos + 1);
    
    // 移除可能的扩展名（如 .zip）
    size_t dotPos = provinceStr.find_first_of(".");
    if (dotPos != std::string::npos) {
        provinceStr = provinceStr.substr(0, dotPos);
    }
    
    try {
        return std::stoi(provinceStr);
    } catch (...) {
        return 0;
    }
}

/**
 * @brief 阶段1：解压并处理单个省份的压缩文件
 * @param zipFilePath 压缩文件路径
 * @param checker BlacklistChecker实例
 * @param provinceCode 省份代码
 * @param counter 线程安全计数器
 */
void processSingleProvinceZip(const std::string& zipFilePath, BlacklistChecker& checker, int provinceCode, ThreadSafeCounter& counter) {
    try {
        std::cout << "Processing province " << provinceCode << " from " << zipFilePath << std::endl;
        
        // 创建 ZipExtractor 实例
        ZipExtractor extractor;
        if (extractor.open(zipFilePath) != ZipExtractor::ZipResult::OK) {
            std::cerr << "Failed to open " << zipFilePath << ": " << extractor.getLastError() << std::endl;
            return;
        }
        
        // 提取压缩文件名作为目标目录名
        std::string zipFileName = zipFilePath.substr(zipFilePath.find_last_of("\\/") + 1);
        size_t dotPos = zipFileName.find_last_of(".");
        if (dotPos != std::string::npos) {
            zipFileName = zipFileName.substr(0, dotPos);
        }
        
        // 目标目录
        std::string extractDestDir = zipFilePath.substr(0, zipFilePath.find_last_of("\\/") + 1) + zipFileName;
        
        // 获取文件列表并统计JSON文件数量
        std::vector<std::string> allFiles = extractor.getFileList();
        std::cout << "ZIP contains " << allFiles.size() << " files" << std::endl;
        for (const auto& f : allFiles) {
            std::cout << "  ZIP entry: '" << f << "'" << std::endl;
        }
        
        std::cout << "Province " << provinceCode << " extractDestDir: " << extractDestDir << std::endl;
        
        // 检查解压目录是否存在
        if (!std::filesystem::exists(extractDestDir)) {
            std::cout << "  ❌ Extract directory does NOT exist: " << extractDestDir << std::endl;
        } else {
            std::cout << "  ✅ Extract directory exists: " << extractDestDir << std::endl;
            // 列出目录中的文件
            std::cout << "  Files in directory: " << std::endl;
            for (const auto& entry : std::filesystem::directory_iterator(extractDestDir)) {
                std::cout << "    - " << entry.path().filename() << std::endl;
            }
        }
        
        // 解压文件
        if (extractor.extractAll(extractDestDir) != ZipExtractor::ZipResult::OK) {
            std::cerr << "Failed to extract " << zipFilePath << ": " << extractor.getLastError() << std::endl;
            extractor.close();
            return;
        }
        extractor.close();

        // 检查解压后目录中的文件
        std::cout << "  After extraction, files in directory: " << std::endl;
        if (std::filesystem::exists(extractDestDir)) {
            for (const auto& entry : std::filesystem::directory_iterator(extractDestDir)) {
                std::cout << "    - " << entry.path().filename() << std::endl;
            }
        }

        // 构建JSON文件路径（解压后）
        std::vector<std::string> jsonFiles;
        for (const auto& filePath : allFiles) {
            std::cout << "  Processing ZIP entry: '" << filePath << "'" << std::endl;
            std::string fname = filePath.substr(filePath.find_last_of("\\/") + 1);
            if (fname.length() > 5 && fname.substr(fname.find_last_of(".") + 1) == "json") {
                // 构建完整的JSON文件路径（解压后的路径）
                std::string fullPath = extractDestDir + "\\" + fname;
                jsonFiles.push_back(fullPath);
                std::cout << "  JSON is at: '" << fullPath << "'" << std::endl;
                
                // 检查文件是否存在
                std::ifstream testFile(fullPath);
                if (testFile.good()) {
                    std::cout << "  ✅ File exists: " << fullPath << std::endl;
                } else {
                    std::cout << "  ❌ File does NOT exist: " << fullPath << std::endl;
                }
                testFile.close();
            }
        }
        
        int jsonCount = jsonFiles.size();
        std::cout << "Province " << provinceCode << " has " << jsonCount << " JSON files" << std::endl;

        // 并行处理该省份的JSON文件
        if (!jsonFiles.empty()) {
            ThreadConfig config = calculateThreadConfig();
            size_t threadCount = config.parseThreads;
            if (threadCount > jsonFiles.size()) {
                threadCount = jsonFiles.size();
            }
            std::cout << "Province " << provinceCode << " using " << threadCount << " JSON processing threads" << std::endl;

            std::vector<std::thread> threads;
            std::atomic<size_t> processedCount(0);

            for (size_t i = 0; i < jsonFiles.size(); ++i) {
                threads.emplace_back([&, i]() {
                    processSingleJsonFileBatch(jsonFiles[i], checker, counter);
                    size_t current = ++processedCount;
                    if (current % 100 == 0 || current == jsonFiles.size()) {
                        std::cout << "Province " << provinceCode << ": " << current << "/" << jsonFiles.size() << " JSON files processed" << std::endl;
                    }
                });

                // 控制并发数
                if (threads.size() >= threadCount) {
                    for (auto& t : threads) {
                        t.join();
                    }
                    threads.clear();
                }
            }

            // 等待所有线程完成
            for (auto& t : threads) {
                t.join();
            }
        }

        std::cout << "Province " << provinceCode << " processing completed" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error processing province " << provinceCode << ": " << e.what() << std::endl;
    }
}

/**
 * @brief 分配文件到线程
 * @param files 压缩文件信息列表
 * @param threadCount 线程数量
 * @return 分配给每个线程的文件列表
 */
std::vector<std::vector<ZipFileInfo>> distributeFiles(const std::vector<ZipFileInfo>& files, size_t threadCount) {
    // 按文件大小降序排序
    std::vector<ZipFileInfo> sortedFiles = files;
    std::sort(sortedFiles.begin(), sortedFiles.end(), [](const ZipFileInfo& a, const ZipFileInfo& b) {
        return a.size > b.size;
    });
    
    // 初始化线程任务
    std::vector<std::vector<ZipFileInfo>> threadTasks(threadCount);
    std::vector<uint64_t> threadSizes(threadCount, 0);
    
    // 分配文件
    for (const auto& file : sortedFiles) {
        // 找到当前总大小最小的线程
        size_t minThread = 0;
        for (size_t i = 1; i < threadCount; ++i) {
            if (threadSizes[i] < threadSizes[minThread]) {
                minThread = i;
            }
        }
        
        // 分配文件
        threadTasks[minThread].push_back(file);
        threadSizes[minThread] += file.size;
    }
    
    return threadTasks;
}

/**
 * @brief 处理目录
 * @param directory 目录路径
 * @param checker BlacklistChecker实例
 * @param loadedCount 成功加载的数量
 * @param invalidCount 无效卡片ID数量
 */
void processDirectory(const std::string& directory, BlacklistChecker& checker, size_t& loadedCount, size_t& invalidCount) {
    try {
        std::cout << "Processing directory: " << directory << std::endl;
        
        // 收集目录中的所有压缩文件
        std::vector<ZipFileInfo> zipFiles;
        std::vector<std::string> subDirectories;
        
        // 搜索目录中的所有文件
        WIN32_FIND_DATAA findData;
        HANDLE hFind = FindFirstFileA((directory + "\\*").c_str(), &findData);
        if (hFind != INVALID_HANDLE_VALUE) {
            int zipCount = 0;
            int dirCount = 0;
            do {
                if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    // 跳过 "." 和 ".."
                    if (strcmp(findData.cFileName, ".") != 0 && strcmp(findData.cFileName, "..") != 0) {
                        subDirectories.push_back(directory + "\\" + findData.cFileName);
                        dirCount++;
                    }
                } else {
                    std::string filePath = directory + "\\" + findData.cFileName;
                    std::string fileName = findData.cFileName;
                    // 检查是否是压缩文件
                    if (fileName.substr(fileName.find_last_of(".") + 1) == "zip") {
                        // 获取文件大小
                        uint64_t size = getFileSize(filePath);
                        zipFiles.push_back({filePath, size});
                        zipCount++;
                    }
                }
            } while (FindNextFileA(hFind, &findData));
            FindClose(hFind);
            std::cout << "Found " << zipCount << " zip files and " << dirCount << " directories in " << directory << std::endl;
        }
        
        // 处理压缩文件
        if (!zipFiles.empty()) {
            // 计算最优线程配置
            ThreadConfig config = calculateThreadConfig();
            size_t threadCount = config.extractThreads;

            // 输出线程数量
            std::cout << "Extraction thread count: " << config.extractThreads << std::endl;
            std::cout << "JSON processing thread count: " << config.parseThreads << std::endl;
            
            // 分配文件
            auto threadTasks = distributeFiles(zipFiles, threadCount);
            
            // 输出分配结果
            for (size_t i = 0; i < threadTasks.size(); ++i) {
                std::cout << "Thread " << i << " has " << threadTasks[i].size() << " files" << std::endl;
            }
            
            // 创建 JSON 文件路径队列
            ThreadSafeQueue<std::string> jsonQueue;
            
            // 创建线程安全计数器
            ThreadSafeCounter counter;
            
            // 启动 JSON 处理线程
            std::vector<std::thread> jsonThreads;
            for (size_t i = 0; i < threadCount; ++i) {
                jsonThreads.emplace_back([&, i]() {
                    std::cout << "JSON thread " << i << " started" << std::endl;
                    while (true) {
                        std::string filePath = jsonQueue.pop();
                        if (filePath.empty()) break;
                        processSingleJsonFileBatch(filePath, checker, counter);
                    }
                    std::cout << "JSON thread " << i << " finished" << std::endl;
                });
            }
            
            // 启动解压线程
            std::vector<std::thread> extractThreads;
            for (size_t i = 0; i < threadTasks.size(); ++i) {
                const auto& task = threadTasks[i];
                extractThreads.emplace_back([&, task, i]() {
                    std::cout << "Extraction thread " << i << " started" << std::endl;
                    for (const auto& zipFile : task) {
                        extractAndCollectJson(zipFile.path, jsonQueue, directory);
                    }
                    std::cout << "Extraction thread " << i << " finished" << std::endl;
                });
            }
            
            // 等待所有解压线程完成
            for (auto& thread : extractThreads) {
                thread.join();
            }
            
            // 标记 JSON 队列完成（所有文件都已添加到队列）
            jsonQueue.setDone();
            
            // 等待所有 JSON 处理线程完成
            for (auto& thread : jsonThreads) {
                thread.join();
            }
            
            // 更新计数器
            loadedCount = counter.loaded;
            invalidCount = counter.invalid;
        }
        
        // 递归处理子目录
        for (const auto& subDir : subDirectories) {
            size_t subLoaded = 0;
            size_t subInvalid = 0;
            processDirectory(subDir, checker, subLoaded, subInvalid);
            loadedCount += subLoaded;
            invalidCount += subInvalid;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error processing directory " << directory << ": " << e.what() << std::endl;
    }
}

/**
 * @brief 按文档方案实现的黑名单加载（优化版）
 * 流程：
 * 1. 一级解压（串行）
 * 2. 收集省份压缩文件（串行）
 * 3. 全局任务队列 + 动态负载均衡（并行）
 *    - 解压线程：4-8个，负责解压二级压缩文件
 *    - 消费者线程：CPU核心数，从全局队列获取json文件处理
 * 4. 全局排序（串行）
 */
bool loadBlacklistFromCompressedFile(const std::string& compressedPath, BlacklistChecker& checker, size_t& totalLoaded, size_t& totalInvalid, const std::string& baseDir) {
    try {
        auto startTime = std::chrono::system_clock::now();
        std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

        LOG_INFO("==========================================");
        LOG_INFO("Starting blacklist load from ZIP");
        std::string timeStr = std::ctime(&start_time);
        LOG_INFO("Start time: %s", timeStr.c_str());
        LOG_INFO("ZIP path: %s", compressedPath.c_str());

        std::cout << "==========================================" << std::endl;
        std::cout << "Start loading blacklist at " << std::ctime(&start_time);
        std::cout << "==========================================" << std::endl;

        LOG_INFO("Stage 1: Extracting primary ZIP file to disk");
        std::cout << "\n[Stage 1] Extracting primary ZIP file to disk..." << std::endl;

        ZipExtractor extractor;
        if (extractor.open(compressedPath) != ZipExtractor::ZipResult::OK) {
            LOG_ERROR("Failed to open compressed file: %s", extractor.getLastError().c_str());
            std::cerr << "Failed to open compressed file: " << extractor.getLastError() << std::endl;
            return false;
        }

        std::string zipFileName = compressedPath.substr(compressedPath.find_last_of("\\/") + 1);
        size_t dotPos = zipFileName.find_last_of(".");
        if (dotPos != std::string::npos) {
            zipFileName = zipFileName.substr(0, dotPos);
        }

        std::string destDir;
        if (!baseDir.empty()) {
            destDir = baseDir + "\\" + zipFileName;
        } else {
            char exePath[MAX_PATH];
            GetModuleFileNameA(NULL, exePath, MAX_PATH);
            std::string exeDir = std::string(exePath);
            exeDir = exeDir.substr(0, exeDir.find_last_of("\\/"));
            destDir = exeDir + "\\" + zipFileName;
        }

        LOG_INFO("Extraction destination: %s", destDir.c_str());
        std::cout << "Extracting to directory: " << destDir << std::endl;

        if (extractor.extractAll(destDir) != ZipExtractor::ZipResult::OK) {
            LOG_ERROR("Primary ZIP extraction failed: %s", extractor.getLastError().c_str());
            std::cerr << "Extraction failed: " << extractor.getLastError() << std::endl;
            return false;
        }

        extractor.close();

        LOG_INFO("Stage 1 completed: Primary ZIP extracted");
        std::cout << "Primary extraction completed" << std::endl;

        LOG_INFO("Stage 2: Scanning for province ZIP files");
        std::cout << "\n[Stage 2] Collecting province ZIP files..." << std::endl;
        auto provinceZips = collectProvinceZips(destDir);
        LOG_INFO("Found %zu province ZIP files", provinceZips.size());
        std::cout << "Found " << provinceZips.size() << " province ZIP files" << std::endl;

        if (provinceZips.empty()) {
            LOG_WARN("No province ZIP files found, nothing to load");
            std::cout << "No province ZIP files found. Loading completed." << std::endl;
            totalLoaded = 0;
            totalInvalid = 0;
            return true;
        }

        LOG_INFO("Stage 3: Starting parallel JSON processing");
        std::cout << "\n[Stage 3] Processing with global queue + consumer pool..." << std::endl;

        ThreadConfig config = calculateThreadConfig(provinceZips.size());

        LOG_INFO("Thread configuration - Extract: %zu, Parse: %zu, Total: %zu, Queue: %zu",
                 config.extractThreads, config.parseThreads, config.totalThreads, config.queueSize);

        std::mutex logMutex;
        auto safeLog = [&](const std::string& message) {
            std::lock_guard<std::mutex> lock(logMutex);
            std::cout << message << std::endl;
        };

        std::cout << "=== Memory Stream Processing Configuration ===" << std::endl;
        std::cout << "CPU Cores: " << getCpuCoreCount() << std::endl;
        std::cout << "Extract Threads: " << config.extractThreads << " (IO intensive)" << std::endl;
        std::cout << "Parse Threads: " << config.parseThreads << " (CPU intensive)" << std::endl;
        std::cout << "Total Threads: " << config.totalThreads << std::endl;
        std::cout << "Queue Size: " << config.queueSize << " JSON tasks" << std::endl;
        std::cout << "Batch Size: " << config.batchSize << std::endl;
        std::cout << "================================================" << std::endl;
        
        // 创建线程安全计数器
        ThreadSafeCounter counter;

        // 全局JSON任务队列（存储JSON内容）
        ThreadSafeQueue<JsonTask> jsonTaskQueue(config.queueSize);

        // 原子变量：已处理的JSON数量
        std::atomic<size_t> processedCount(0);
        std::atomic<size_t> totalJsonFiles(0);
        
        // 省份统计：跟踪每个省份的JSON数量和已处理数量
        struct ProvinceStats {
            std::atomic<size_t> totalJsons = 0;
            std::atomic<size_t> processedJsons = 0;
        };
        std::unordered_map<int, ProvinceStats> provinceStats;
        std::mutex provinceStatsMutex;
        
        // 消费者线程：从全局队列获取JSON任务并处理
        auto consumerWorker = [&]() {
            try {
                while (true) {
                    JsonTask task = jsonTaskQueue.pop(std::chrono::seconds(2));

                    if (task.isTermination()) {
                        break;
                    }

                    if (task.isEmpty()) {
                        continue;
                    }

                    bool success = processJsonContentFromMemory(task.content, task.filePath, checker, counter);
                    if (!success) {
                        std::cerr << "[Worker] Failed to process: " << task.filePath << std::endl;
                    }

                    size_t current = ++processedCount;

                    if (task.provinceCode > 0) {
                        std::lock_guard<std::mutex> lock(provinceStatsMutex);
                        size_t processed = ++provinceStats[task.provinceCode].processedJsons;
                        size_t total = provinceStats[task.provinceCode].totalJsons;

                        if (total > 0 && processed >= total) {
                            checker.sortProvince(task.provinceCode);
                            safeLog("Province " + std::to_string(task.provinceCode) +
                                   " sort completed (" + std::to_string(processed) + "/" +
                                   std::to_string(total) + " JSON files)");
                        }
                    }

                    if (current % 100 == 0) {
                        safeLog("Progress: " + std::to_string(current) + "/" +
                               std::to_string(totalJsonFiles.load()) + " JSON files processed");
                    }
                }
            } catch (const std::exception& e) {
                LOG_ERROR("Consumer worker exception: %s", e.what());
                std::cerr << "[Worker] Unexpected error: " << e.what() << std::endl;
            }
            LOG_DEBUG("Consumer thread exiting");
        };

        std::cout << "Started " << config.parseThreads << " consumer threads" << std::endl;
        LOG_INFO("Started %zu consumer threads", config.parseThreads);

        std::vector<std::thread> consumerThreads;
        for (size_t i = 0; i < config.parseThreads; ++i) {
            consumerThreads.emplace_back(consumerWorker);
        }

        ThreadSafeQueue<ProvinceZipInfo> provinceQueue;

        for (const auto& province : provinceZips) {
            provinceQueue.push(province);
        }

        LOG_INFO("Added %zu provinces to task queue", provinceZips.size());
        std::cout << "Added " << provinceZips.size() << " provinces to task queue" << std::endl;
        
        // 解压线程：固定数量，从省份队列获取任务
        std::vector<std::thread> extractThreads;
        
        // 启动解压线程池
        for (size_t i = 0; i < config.extractThreads; ++i) {
            extractThreads.emplace_back([&, i]() {
                try {
                    while (true) {
                        // 从队列获取省份任务
                        ProvinceZipInfo provinceInfo = provinceQueue.pop();
                        
                        // 收到空省份（终止信号），退出
                        if (provinceInfo.provinceCode == 0) {
                            break;
                        }
                        
                        safeLog("Extract thread " + std::to_string(i) + ": Processing province " +
                               std::to_string(provinceInfo.provinceCode));

                        ZipExtractor extractor;
                        if (extractor.open(provinceInfo.zipPath) != ZipExtractor::ZipResult::OK) {
                            std::cerr << "Failed to open " << provinceInfo.zipPath << ": "
                                     << extractor.getLastError() << std::endl;
                            continue;
                        }

                        std::string logMsg = "Extract thread " + std::to_string(i) + ": Province " +
                                           std::to_string(provinceInfo.provinceCode) +
                                           " reading JSON files directly to memory...";
                        safeLog(logMsg);

                        std::vector<std::string> jsonFileNames;
                        for (const auto& fname : extractor.getFileList()) {
                            std::string name;
                            size_t lastSlash = fname.find_last_of("\\/");
                            if (lastSlash != std::string::npos) {
                                name = fname.substr(lastSlash + 1);
                            } else {
                                name = fname;
                            }
                            size_t dotPos = name.find_last_of(".");
                            if (dotPos != std::string::npos && dotPos > 0 &&
                                name.substr(dotPos + 1) == "json") {
                                jsonFileNames.push_back(fname);
                            }
                        }

                        size_t jsonCount = jsonFileNames.size();
                        totalJsonFiles.fetch_add(jsonCount);

                        {
                            std::lock_guard<std::mutex> lock(provinceStatsMutex);
                            provinceStats[provinceInfo.provinceCode].totalJsons = jsonCount;
                        }

                        if (jsonCount == 0) {
                            std::cerr << "[Extract] No JSON files found in " << provinceInfo.zipPath << std::endl;
                            extractor.close();
                            if (DeleteFileA(provinceInfo.zipPath.c_str())) {
                                safeLog("Extract thread " + std::to_string(i) + ": Province " +
                                       std::to_string(provinceInfo.provinceCode) + " empty ZIP deleted");
                            }
                            continue;
                        }

                        size_t totalCards = jsonCount * 1000 + 1000;
                        checker.reserveProvinceCapacity(provinceInfo.provinceCode, totalCards);

                        safeLog("Extract thread " + std::to_string(i) + ": Province " +
                               std::to_string(provinceInfo.provinceCode) + " found " +
                               std::to_string(jsonCount) + " JSON files, streaming to memory...");

                        std::atomic<size_t> pushedCount(0);
                        std::atomic<size_t> failedCount(0);

                        for (const auto& jsonFileName : jsonFileNames) {
                            std::string content;
                            ZipExtractor::ZipResult result = extractor.readFileContent(jsonFileName, content);
                            if (result != ZipExtractor::ZipResult::OK) {
                                std::cerr << "[Extract] Failed to read " << jsonFileName
                                          << ": " << extractor.getLastError() << std::endl;
                                failedCount++;
                                continue;
                            }

                            std::string fileName;
                            size_t lastSlash = jsonFileName.find_last_of("\\/");
                            if (lastSlash != std::string::npos) {
                                fileName = jsonFileName.substr(lastSlash + 1);
                            } else {
                                fileName = jsonFileName;
                            }

                            JsonTask task(provinceInfo.provinceCode, std::move(content), fileName);

                            int retryCount = 0;
                            while (true) {
                                JsonTask taskToSend = task;
                                if (jsonTaskQueue.push(std::move(taskToSend), std::chrono::seconds(5))) {
                                    pushedCount++;
                                    break;
                                }
                                retryCount++;
                                if (retryCount >= MAX_RETRY_COUNT) {
                                    std::cerr << "[Extract] Failed to push after " << MAX_RETRY_COUNT
                                              << " attempts, skipping: " << fileName << std::endl;
                                    failedCount++;
                                    break;
                                }
                            }
                        }

                        extractor.close();

                        safeLog("Extract thread " + std::to_string(i) + ": Province " +
                                std::to_string(provinceInfo.provinceCode) + " queued " +
                                std::to_string(pushedCount.load()) + " JSON files, failed: " +
                                std::to_string(failedCount.load()));

                        if (DeleteFileA(provinceInfo.zipPath.c_str())) {
                            safeLog("Extract thread " + std::to_string(i) + ": Province " +
                                   std::to_string(provinceInfo.provinceCode) + " ZIP deleted (memory stream mode)");
                        }

                        safeLog("Extract thread " + std::to_string(i) + ": Province " +
                               std::to_string(provinceInfo.provinceCode) + " completed");
                    }
                } catch (const std::exception& e) {
                    safeLog("Extract thread " + std::to_string(i) + " error - " + e.what());
                }
            });
        }
        
        safeLog("Sending termination signals to province queue...");
        ProvinceZipInfo terminator;
        terminator.provinceCode = 0;
        terminator.zipPath = "";
        for (size_t i = 0; i < config.extractThreads; ++i) {
            provinceQueue.push(terminator);
        }

        LOG_INFO("Sending termination signals to extract threads");
        safeLog("Waiting for all extract threads to complete...");
        for (auto& t : extractThreads) {
            t.join();
        }
        LOG_INFO("All %zu extract threads completed", config.extractThreads);
        safeLog("All extract threads completed");

        LOG_INFO("Sending termination signals to %zu consumer threads", config.parseThreads);
        safeLog("Sending termination signals to consumer threads...");
        for (size_t i = 0; i < config.parseThreads; ++i) {
            jsonTaskQueue.push(JsonTask::createTerminationTask());
        }

        LOG_INFO("Waiting for all consumer threads to complete");
        safeLog("Waiting for all consumer threads to complete...");
        for (auto& t : consumerThreads) {
            t.join();
        }
        LOG_INFO("All %zu consumer threads completed", config.parseThreads);
        safeLog("All consumer threads completed");

        LOG_INFO("Stage 4: Starting global sort");
        std::cout << "\n[Stage 4] Sorting all data..." << std::endl;
        checker.sortAll();
        LOG_INFO("Stage 4: Global sort completed");
        
        auto endTime = std::chrono::system_clock::now();
        std::chrono::duration<double> elapsedSeconds = endTime - startTime;
        double totalSeconds = elapsedSeconds.count();

        std::cout << "\n==========================================" << std::endl;
        std::cout << "Loading Statistics" << std::endl;
        std::cout << "==========================================" << std::endl;
        std::cout << "Total provinces processed: " << provinceZips.size() << std::endl;
        std::cout << "Total blacklist loaded: " << counter.loaded << std::endl;
        std::cout << "Invalid cards: " << counter.invalid << std::endl;
        std::cout << "Total loading time: " << totalSeconds << " seconds" << std::endl;
        std::cout << "==========================================" << std::endl;

        LOG_INFO("========== Loading Statistics ==========");
        LOG_INFO("Total provinces: %zu", provinceZips.size());
        LOG_INFO("Total loaded: %zu", counter.loaded.load());
        LOG_INFO("Invalid cards: %zu", counter.invalid.load());
        LOG_INFO("Total time: %.2f seconds", totalSeconds);
        LOG_INFO("==========================================");

        totalLoaded = counter.loaded;
        totalInvalid = counter.invalid;

        LOG_INFO("Blacklist loading completed successfully");
        return true;
    } catch (const std::exception& e) {
        LOG_FATAL("Exception in loadBlacklistFromCompressedFile: %s", e.what());
        std::cerr << "Error loading blacklist from compressed file: " << e.what() << std::endl;
        return false;
    }
}
