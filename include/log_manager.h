#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include <string>
#include <atomic>
#include <mutex>
#include <memory>
#include <fstream>
#include <functional>
#include <cstdarg>

constexpr size_t MAX_LOG_FILE_SIZE = 20ULL * 1024 * 1024;

class LogManager {
public:
    static LogManager& getInstance();

    enum Level {
        DEBUG = 0,
        INFO = 1,
        WARN = 2,
        ERR = 3,       // 使用ERR避免Windows ERROR宏冲突
        FATAL = 4
    };

    void init(const std::string& logDir = "logs", Level minLevel = DEBUG);

    void setLevel(Level level);
    void flush();

    Level getMinLevel() const { return minLevel_; }

    void writeLog(Level level, const char* message);

private:
    LogManager() : minLevel_(DEBUG), currentFileSize_(0), fileIndex_(0) {}
    ~LogManager() = default;
    LogManager(const LogManager&) = delete;
    LogManager& operator=(const LogManager&) = delete;

    std::string getLogFilePath();
    void rollLogFileIfNeeded();
    void ensureDirectoryExists(const std::string& path);
    std::string formatCurrentTime();
    void writeToFile(const char* log, size_t len);

    static void writeCallback(const void* data, size_t size);

    std::string logDir_;
    Level minLevel_;
    std::atomic<size_t> currentFileSize_;
    int fileIndex_;
    std::mutex mutex_;
    std::unique_ptr<std::ofstream> logStream_;
};

#define LOG_DEBUG(fmt, ...) \
    do { if (LogManager::getInstance().getMinLevel() <= LogManager::DEBUG) \
        { char buf[4096]; snprintf(buf, sizeof(buf), "[DEBUG] " fmt, ##__VA_ARGS__); \
          LogManager::getInstance().writeLog(LogManager::DEBUG, buf); } } while(0)

#define LOG_INFO(fmt, ...) \
    do { if (LogManager::getInstance().getMinLevel() <= LogManager::INFO) \
        { char buf[4096]; snprintf(buf, sizeof(buf), "[INFO ] " fmt, ##__VA_ARGS__); \
          LogManager::getInstance().writeLog(LogManager::INFO, buf); } } while(0)

#define LOG_WARN(fmt, ...) \
    do { if (LogManager::getInstance().getMinLevel() <= LogManager::WARN) \
        { char buf[4096]; snprintf(buf, sizeof(buf), "[WARN ] " fmt, ##__VA_ARGS__); \
          LogManager::getInstance().writeLog(LogManager::WARN, buf); } } while(0)

#define LOG_ERROR(fmt, ...) \
    do { if (LogManager::getInstance().getMinLevel() <= LogManager::ERR) \
        { char buf[4096]; snprintf(buf, sizeof(buf), "[ERROR] " fmt, ##__VA_ARGS__); \
          LogManager::getInstance().writeLog(LogManager::ERR, buf); } } while(0)

#define LOG_FATAL(fmt, ...) \
    do { char buf[4096]; snprintf(buf, sizeof(buf), "[FATAL] " fmt, ##__VA_ARGS__); \
         LogManager::getInstance().writeLog(LogManager::FATAL, buf); } while(0)

#define LOG_IF(cond, level, fmt, ...) \
    do { if (cond) { \
        if (level == LogManager::DEBUG) LOG_DEBUG(fmt, ##__VA_ARGS__); \
        else if (level == LogManager::INFO) LOG_INFO(fmt, ##__VA_ARGS__); \
        else if (level == LogManager::WARN) LOG_WARN(fmt, ##__VA_ARGS__); \
        else if (level == LogManager::ERR) LOG_ERROR(fmt, ##__VA_ARGS__); \
        else LOG_FATAL(fmt, ##__VA_ARGS__); } } while(0)

#define LOG_EVERY_N(n, level, fmt, ...) \
    do { static int _cnt = 0; \
        if (atomic_fetch_add(&_cnt, 1, std::memory_order_relaxed) % (n) == 0) \
            LOG_IF(true, level, fmt, ##__VA_ARGS__); } while(0)

#endif // LOG_MANAGER_H
