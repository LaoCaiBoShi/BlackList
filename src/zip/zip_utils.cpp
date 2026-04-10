#include "zip_utils.h"
#include "zip_basic.h"
#include "blacklist_checker.h"
#include "ZipExtractor.h"
#include "thread_pool.h"
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

// Windows 特定头文件
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

// 全局计数器
std::atomic<size_t> globalLoadedCount(0);

/**
 * @brief 线程安全队列，用于存储 JSON 文件路径
 */
template <typename T>
class ThreadSafeQueue {
public:
    /**
     * @brief 向队列中添加元素
     * @param item 要添加的元素
     */
    void push(T item) {
        std::lock_guard<std::mutex> lock(mutex);
        queue.push(std::move(item));
        condition.notify_one();
    }
    
    /**
     * @brief 从队列中获取元素
     * @return 队列中的元素，如果队列为空且标记为完成，则返回默认构造的元素
     */
    T pop() {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [this]() { return !queue.empty() || done; });
        if (queue.empty()) return T();
        T item = std::move(queue.front());
        queue.pop();
        return item;
    }
    
    /**
     * @brief 标记队列完成
     */
    void setDone() {
        std::lock_guard<std::mutex> lock(mutex);
        done = true;
        condition.notify_all();
    }
    
    /**
     * @brief 检查队列是否为空
     * @return 队列是否为空
     */
    bool empty() {
        std::lock_guard<std::mutex> lock(mutex);
        return queue.empty();
    }
    
private:
    std::queue<T> queue;       // 存储元素的队列
    std::mutex mutex;           // 互斥锁
    std::condition_variable condition; // 条件变量
    bool done = false;          // 完成标记
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
        if (fileName.substr(fileName.find_last_of(".") + 1) != "json") {
            std::cerr << "Not a JSON file: " << fileName << std::endl;
            return false;
        }

        // 打开文件
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Failed to open file: " << filePath << std::endl;
            return false;
        }
        
        std::cout << "Successfully opened: " << filePath << std::endl;

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
                    cardIds.push_back(cardId);
                }
            }
        }

        return true;
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
void processSingleJsonFileBatch(const std::string& filePath, BlacklistChecker& checker, ThreadSafeCounter& counter, size_t batchSize = 1000) {
    try {
        std::cout << "Processing JSON file: " << filePath << std::endl;
        // 提取卡号
        std::vector<std::string> cardIds;
        if (extractCardIdsFromJson(filePath, cardIds)) {
            std::cout << "Extracted " << cardIds.size() << " card IDs from " << filePath << std::endl;
            // 批量添加
            if (!cardIds.empty()) {
                checker.addBatch(cardIds);
                counter.addLoaded(cardIds.size());
                // 更新全局计数器
                globalLoadedCount += cardIds.size();
                // 每100000条显示当前的黑名单总数量
                if (globalLoadedCount % 100000 == 0 && globalLoadedCount > 0) {
                    std::cout << "Current blacklist total: " << globalLoadedCount << " items" << std::endl;
                }
            }
        } else {
            std::cerr << "Failed to extract card IDs from: " << filePath << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error processing file " << filePath << ": " << e.what() << std::endl;
    }
}

/**
 * @brief 解压压缩文件并收集JSON文件
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
        auto fileCallback = [&](const std::string& filePathInZip) {
            // 检查是否是 JSON 文件
            std::string fileName = filePathInZip.substr(filePathInZip.find_last_of("\\/") + 1);
            if (fileName.length() > 5 && fileName.substr(fileName.find_last_of(".") + 1) == "json") {
                // 使用实际解压后的路径
                std::string extractedPath = extractDestDir + "\\" + fileName;
                jsonQueue.push(extractedPath);
                jsonCount++;
                std::cout << "Added JSON file to queue: " << fileName << std::endl;
            }
        };
        
        // 解压所有文件
        if (extractor.extractAllWithCallback(extractDestDir, fileCallback) != ZipExtractor::ZipResult::OK) {
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
 * @brief 提取省份代码（二级压缩文件名如20230620_11的后两位）
 * @param fileName 文件名
 * @return 省份代码，如果提取失败返回0（默认省份）
 */
int extractProvinceCode(const std::string& fileName) {
    if (fileName.length() < 2) return 0;
    std::string suffix = fileName.substr(fileName.length() - 2);
    if (suffix.find('_') != std::string::npos) {
        suffix = suffix.substr(suffix.find('_') + 1);
    }
    try {
        return std::stoi(suffix);
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
        std::vector<std::string> jsonFiles;
        for (const auto& filePath : allFiles) {
            std::string fname = filePath.substr(filePath.find_last_of("\\/") + 1);
            if (fname.substr(fname.find_last_of(".") + 1) == "json") {
                jsonFiles.push_back(filePath);
            }
        }
        
        int jsonCount = jsonFiles.size();
        std::cout << "Province " << provinceCode << " has " << jsonCount << " JSON files" << std::endl;

        // 解压文件
        if (extractor.extractAll(extractDestDir) != ZipExtractor::ZipResult::OK) {
            std::cerr << "Failed to extract " << zipFilePath << ": " << extractor.getLastError() << std::endl;
            extractor.close();
            return;
        }
        extractor.close();

        // 并行处理该省份的JSON文件
        if (!jsonFiles.empty()) {
            size_t threadCount = std::thread::hardware_concurrency();
            if (threadCount == 0) threadCount = 4;
            if (threadCount > jsonFiles.size()) {
                threadCount = jsonFiles.size();
            }

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
            // 获取线程数
            size_t threadCount = std::thread::hardware_concurrency();
            if (threadCount == 0) threadCount = 4; // 默认值
            
            // 输出线程数量
            std::cout << "Extraction thread count: " << threadCount << std::endl;
            std::cout << "JSON processing thread count: " << threadCount << std::endl;
            
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
 * @brief 从压缩文件中加载黑名单数据
 * @param compressedPath 压缩文件路径
 * @param checker BlacklistChecker实例
 * @param totalLoaded 成功加载的数量
 * @param totalInvalid 无效卡片ID数量
 * @param baseDir 基础目录，可选，默认为空
 * @return 加载是否成功
 */
bool loadBlacklistFromCompressedFile(const std::string& compressedPath, BlacklistChecker& checker, size_t& totalLoaded, size_t& totalInvalid, const std::string& baseDir) {
    try {
        // 记录开始时间
        auto startTime = std::chrono::system_clock::now();
        std::time_t start_time = std::chrono::system_clock::to_time_t(startTime);
        std::cout << "Start extracting at " << std::ctime(&start_time);
        
        std::cout << "Starting to load blacklist..." << std::endl;
        
        // 创建 ZipExtractor 实例
        ZipExtractor extractor;
        
        // 打开压缩文件
        if (extractor.open(compressedPath) != ZipExtractor::ZipResult::OK) {
            std::cerr << "Failed to open compressed file: " << extractor.getLastError() << std::endl;
            return false;
        }
        
        // 提取压缩文件名（不含扩展名）作为目标目录名
        std::string zipFileName = compressedPath.substr(compressedPath.find_last_of("\\/") + 1);
        size_t dotPos = zipFileName.find_last_of(".");
        if (dotPos != std::string::npos) {
            zipFileName = zipFileName.substr(0, dotPos);
        }
        
        // 构建目标目录路径
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
        
        std::cout << "Extracting to directory: " << destDir << std::endl;
        
        // 解压所有文件
        if (extractor.extractAll(destDir) != ZipExtractor::ZipResult::OK) {
            std::cerr << "Extraction failed: " << extractor.getLastError() << std::endl;
            return false;
        }
        
        // 关闭压缩文件
        extractor.close();
        
        // 记录解压完成时间
        auto extractEndTime = std::chrono::system_clock::now();
        std::time_t extract_end_time = std::chrono::system_clock::to_time_t(extractEndTime);
        std::cout << "End extracting at " << std::ctime(&extract_end_time);
        
        // 处理目录中的文件
        std::cout << "\n==========================================" << std::endl;
        std::cout << "Processing directory..." << std::endl;
        std::cout << "==========================================" << std::endl;
        
        size_t loaded = 0;
        size_t invalid = 0;
        processDirectory(destDir, checker, loaded, invalid);

        totalLoaded = loaded;
        totalInvalid = invalid;

        // 对所有数据进行排序
        checker.sortAll();

        // 计算总加载时间
        auto endTime = std::chrono::system_clock::now();
        std::chrono::duration<double> elapsedSeconds = endTime - startTime;
        double totalSeconds = elapsedSeconds.count();

        std::cout << "\n==========================================" << std::endl;
        std::cout << "Loading Statistics" << std::endl;
        std::cout << "==========================================" << std::endl;
        std::cout << "Total blacklist loaded: " << totalLoaded << std::endl;
        std::cout << "Total loading time: " << totalSeconds << " seconds" << std::endl;
        std::cout << "==========================================" << std::endl;

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error loading blacklist from compressed file: " << e.what() << std::endl;
        return false;
    }
}
