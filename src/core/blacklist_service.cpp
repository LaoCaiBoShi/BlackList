/**
 * @file blacklist_service.cpp
 * @brief 黑名单服务封装类实现
 */

#include "blacklist_service.h"
#include "zip_utils.h"
#include <iostream>
#include <chrono>

BlacklistService::BlacklistService()
    : status_(Status::UNINITIALIZED) {
    checker_ = std::make_unique<BlacklistChecker>();
}

BlacklistService::~BlacklistService() {
    if (updateThread_ && updateThread_->joinable()) {
        updateThread_->join();
    }
}

bool BlacklistService::initialize(const std::string& zipPath, const std::string& persistPath) {
    std::lock_guard<std::mutex> lock(pathMutex_);
    currentZipPath_ = zipPath;

    // 尝试从持久化文件快速恢复
    if (checker_->isPersistFileValid(persistPath)) {
        std::cout << "[BlacklistService] Found valid persist file, restoring..." << std::endl;
        if (checker_->loadFromPersistFile(persistPath)) {
            std::cout << "[BlacklistService] Restored " << checker_->size()
                      << " records from persist file" << std::endl;

            // 立即进入就绪状态
            {
                std::lock_guard<std::mutex> statusLock(statusMutex_);
                status_ = Status::READY;
            }
            statusCV_.notify_all();

            // 启动后台线程加载最新数据
            std::cout << "[BlacklistService] Starting background loading for fresh data..." << std::endl;
            {
                std::lock_guard<std::mutex> statusLock(statusMutex_);
                status_ = Status::UPDATING;
            }

            updateThread_ = std::make_unique<std::thread>(
                &BlacklistService::backgroundLoadingThread, this, zipPath);

            return true;
        }
    }

    // 没有持久化文件或恢复失败，同步加载
    std::cout << "[BlacklistService] No valid persist file, loading from ZIP synchronously..." << std::endl;
    {
        std::lock_guard<std::mutex> statusLock(statusMutex_);
        status_ = Status::LOADING;
    }
    statusCV_.notify_all();

    size_t loaded = 0, invalid = 0;
    bool success = loadBlacklistFromCompressedFile(zipPath, *checker_, loaded, invalid);

    if (success) {
        std::cout << "[BlacklistService] Loaded " << loaded
                  << " records from ZIP" << std::endl;

        // 保存持久化文件以便下次快速恢复
        std::cout << "[BlacklistService] Saving persist file for future fast recovery..." << std::endl;
        checker_->savePersistAfterLoad(persistPath);

        {
            std::lock_guard<std::mutex> statusLock(statusMutex_);
            status_ = Status::READY;
        }
        statusCV_.notify_all();
        return true;
    } else {
        setError("Failed to load from ZIP file");
        {
            std::lock_guard<std::mutex> statusLock(statusMutex_);
            status_ = Status::ERROR;
        }
        statusCV_.notify_all();
        return false;
    }
}

bool BlacklistService::isBlacklisted(const std::string& cardId) {
    return checker_->isBlacklisted(cardId);
}

void BlacklistService::triggerUpdate(const std::string& zipPath) {
    std::lock_guard<std::mutex> lock(pathMutex_);
    currentZipPath_ = zipPath;

    Status currentStatus = status_.load();
    if (currentStatus == Status::LOADING) {
        std::cout << "[BlacklistService] Already loading, ignoring update request" << std::endl;
        return;
    }

    if (currentStatus == Status::UPDATING) {
        std::cout << "[BlacklistService] Already updating, ignoring update request" << std::endl;
        return;
    }

    // 等待现有更新线程结束
    if (updateThread_ && updateThread_->joinable()) {
        std::cout << "[BlacklistService] Waiting for previous update to finish..." << std::endl;
        updateThread_->join();
    }

    // 启动新的更新线程
    {
        std::lock_guard<std::mutex> statusLock(statusMutex_);
        status_ = Status::UPDATING;
    }
    statusCV_.notify_all();

    updateThread_ = std::make_unique<std::thread>(
        &BlacklistService::backgroundLoadingThread, this, zipPath);
}

bool BlacklistService::syncUpdate(const std::string& zipPath) {
    // 等待当前状态变为READY（如果在加载中）
    waitForReady(30000);  // 等待最多30秒

    Status currentStatus = status_.load();
    if (currentStatus == Status::ERROR) {
        std::cerr << "[BlacklistService] Cannot update from ERROR state" << std::endl;
        return false;
    }

    std::cout << "[BlacklistService] Starting synchronous update..." << std::endl;

    {
        std::lock_guard<std::mutex> statusLock(statusMutex_);
        status_ = Status::UPDATING;
    }
    statusCV_.notify_all();

    size_t loaded = 0, invalid = 0;
    bool success = loadBlacklistFromCompressedFile(zipPath, *checker_, loaded, invalid);

    if (success) {
        std::cout << "[BlacklistService] Sync update completed: " << loaded << " records" << std::endl;
        {
            std::lock_guard<std::mutex> statusLock(statusMutex_);
            status_ = Status::READY;
        }
        statusCV_.notify_all();
        return true;
    } else {
        setError("Sync update failed");
        {
            std::lock_guard<std::mutex> statusLock(statusMutex_);
            status_ = Status::ERROR;
        }
        statusCV_.notify_all();
        return false;
    }
}

BlacklistService::Status BlacklistService::getStatus() const {
    return status_.load();
}

std::string BlacklistService::getStatusString() const {
    switch (status_.load()) {
        case Status::UNINITIALIZED: return "UNINITIALIZED";
        case Status::LOADING: return "LOADING";
        case Status::READY: return "READY";
        case Status::UPDATING: return "UPDATING";
        case Status::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

std::string BlacklistService::getVersionInfo() const {
    return checker_->getVersionInfo();
}

size_t BlacklistService::getBlacklistSize() const {
    return checker_->size();
}

bool BlacklistService::isPersistFileValid(const std::string& persistPath) const {
    BlacklistChecker tempChecker;
    return tempChecker.isPersistFileValid(persistPath);
}

bool BlacklistService::waitForReady(int timeoutMs) {
    std::unique_lock<std::mutex> lock(statusMutex_);
    if (timeoutMs <= 0) {
        statusCV_.wait(lock, [this] {
            Status s = status_.load();
            return s == Status::READY || s == Status::ERROR;
        });
        return status_.load() == Status::READY;
    } else {
        auto deadline = std::chrono::system_clock::now() +
                        std::chrono::milliseconds(timeoutMs);
        bool wokeUp = statusCV_.wait_until(lock, deadline, [this] {
            Status s = status_.load();
            return s == Status::READY || s == Status::ERROR;
        });
        return wokeUp && status_.load() == Status::READY;
    }
}

std::string BlacklistService::getLastError() const {
    std::lock_guard<std::mutex> lock(errorMutex_);
    return lastError_;
}

bool BlacklistService::savePersistFile(const std::string& persistPath) {
    if (status_.load() == Status::UNINITIALIZED || status_.load() == Status::ERROR) {
        std::cerr << "[BlacklistService] Cannot save persist file in current state" << std::endl;
        return false;
    }
    return checker_->savePersistAfterLoad(persistPath);
}

bool BlacklistService::isRunning() const {
    Status s = status_.load();
    return s == Status::READY || s == Status::UPDATING;
}

void BlacklistService::backgroundLoadingThread(const std::string& zipPath) {
    std::cout << "[BlacklistService] Background loading started..." << std::endl;

    // 创建临时checker用于加载新数据
    auto tempChecker = std::make_unique<BlacklistChecker>();

    size_t loaded = 0, invalid = 0;
    bool success = loadBlacklistFromCompressedFile(zipPath, *tempChecker, loaded, invalid);

    if (success) {
        std::cout << "[BlacklistService] Background loading completed: " << loaded << " records" << std::endl;

        // 原子切换数据
        {
            std::lock_guard<std::mutex> lock(pathMutex_);
            checker_ = std::move(tempChecker);
        }

        // 保存新的持久化文件
        std::cout << "[BlacklistService] Saving new persist file..." << std::endl;
        checker_->savePersistAfterLoad("blacklist.dat");

        {
            std::lock_guard<std::mutex> statusLock(statusMutex_);
            status_ = Status::READY;
        }
        statusCV_.notify_all();

        std::cout << "[BlacklistService] Background update finished, now serving new data" << std::endl;
    } else {
        std::cerr << "[BlacklistService] Background loading failed!" << std::endl;
        setError("Background loading failed");
        {
            std::lock_guard<std::mutex> statusLock(statusMutex_);
            status_ = Status::ERROR;
        }
        statusCV_.notify_all();
    }
}

void BlacklistService::setError(const std::string& errorMsg) {
    std::lock_guard<std::mutex> lock(errorMutex_);
    lastError_ = errorMsg;
    std::cerr << "[BlacklistService] ERROR: " << errorMsg << std::endl;
}