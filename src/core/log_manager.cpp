#include "log_manager.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <cstring>

LogManager& LogManager::getInstance() {
    static LogManager instance;
    return instance;
}

void LogManager::init(const std::string& logDir, Level minLevel) {
    logDir_ = logDir;
    minLevel_ = minLevel;
    currentFileSize_.store(0);
    fileIndex_ = 0;

    ensureDirectoryExists(logDir_);

    std::string path = getLogFilePath();
    logStream_ = std::make_unique<std::ofstream>(path, std::ios::app);

    if (logStream_->is_open()) {
        writeLog(INFO, "LogManager initialized, log file: logs");
    }
}

void LogManager::setLevel(Level level) {
    minLevel_ = level;
}

void LogManager::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (logStream_ && logStream_->is_open()) {
        logStream_->flush();
    }
}

std::string LogManager::formatCurrentTime() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf;
#ifdef _WIN32
    localtime_s(&tmBuf, &t);
#else
    localtime_r(&t, &tmBuf);
#endif

    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmBuf);
    return std::string(buf);
}

std::string LogManager::getLogFilePath() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf;
#ifdef _WIN32
    localtime_s(&tmBuf, &t);
#else
    localtime_r(&t, &tmBuf);
#endif

    char date[32];
    strftime(date, sizeof(date), "%Y_%m_%d", &tmBuf);

    char path[512];
    if (fileIndex_ == 0) {
        snprintf(path, sizeof(path), "%s/%04d/%02d/app_%s.log",
                 logDir_.c_str(), tmBuf.tm_year + 1900, tmBuf.tm_mon + 1, date);
    } else {
        snprintf(path, sizeof(path), "%s/%04d/%02d/app_%s_%d.log",
                 logDir_.c_str(), tmBuf.tm_year + 1900, tmBuf.tm_mon + 1, date, fileIndex_);
    }
    return std::string(path);
}

void LogManager::ensureDirectoryExists(const std::string& path) {
    try {
        std::filesystem::create_directories(path);
    } catch (const std::exception& e) {
        fprintf(stderr, "Failed to create directory %s: %s\n", path.c_str(), e.what());
    }
}

void LogManager::rollLogFileIfNeeded() {
    if (currentFileSize_.load() >= MAX_LOG_FILE_SIZE) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (currentFileSize_.load() >= MAX_LOG_FILE_SIZE) {
            if (logStream_ && logStream_->is_open()) {
                logStream_->flush();
                logStream_->close();
            }

            fileIndex_++;
            currentFileSize_.store(0);

            std::string path = getLogFilePath();
            size_t lastSlash = path.find_last_of("\\/");
            if (lastSlash != std::string::npos) {
                ensureDirectoryExists(path.substr(0, lastSlash));
            }
            logStream_ = std::make_unique<std::ofstream>(path, std::ios::app);
        }
    }
}

void LogManager::writeToFile(const char* log, size_t len) {
    if (!logStream_ || !logStream_->is_open()) {
        return;
    }

    logStream_->write(log, len);
    logStream_->flush();
    currentFileSize_.fetch_add(len);
}

void LogManager::writeLog(Level level, const char* message) {
    std::lock_guard<std::mutex> lock(mutex_);

    rollLogFileIfNeeded();

    std::string timeStr = formatCurrentTime();
    std::string logEntry = "[" + timeStr + "] " + std::string(message) + "\n";
    writeToFile(logEntry.c_str(), logEntry.size());
}

void LogManager::writeCallback(const void* data, size_t size) {
    (void)size;
    LogManager::getInstance().writeLog(INFO, static_cast<const char*>(data));
}
