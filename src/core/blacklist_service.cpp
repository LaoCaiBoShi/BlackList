/**
 * @file blacklist_service.cpp
 * @brief 黑名单服务封装类实现（简化版，无持久化）
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
}

bool BlacklistService::initialize(const std::string& zipPath) {
    std::cout << "[BlacklistService] Loading from ZIP synchronously..." << std::endl;
    {
        std::lock_guard<std::mutex> statusLock(statusMutex_);
        status_ = Status::LOADING;
    }

    size_t loaded = 0, invalid = 0;
    bool success = loadBlacklistFromCompressedFile(zipPath, *checker_, loaded, invalid);

    if (success) {
        std::cout << "[BlacklistService] Loaded " << loaded
                  << " records from ZIP" << std::endl;
        {
            std::lock_guard<std::mutex> statusLock(statusMutex_);
            status_ = Status::READY;
        }
        return true;
    } else {
        setError("Failed to load from ZIP file");
        {
            std::lock_guard<std::mutex> statusLock(statusMutex_);
            status_ = Status::ERROR;
        }
        return false;
    }
}

bool BlacklistService::isBlacklisted(const std::string& cardId) {
    return checker_->isBlacklisted(cardId);
}

BlacklistService::Status BlacklistService::getStatus() const {
    return status_.load();
}

std::string BlacklistService::getStatusString() const {
    switch (status_.load()) {
        case Status::UNINITIALIZED: return "UNINITIALIZED";
        case Status::LOADING: return "LOADING";
        case Status::READY: return "READY";
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

bool BlacklistService::isRunning() const {
    Status s = status_.load();
    return s == Status::READY;
}

void BlacklistService::setError(const std::string& errorMsg) {
    std::lock_guard<std::mutex> lock(errorMutex_);
    lastError_ = errorMsg;
    std::cerr << "[BlacklistService] ERROR: " << errorMsg << std::endl;
}
