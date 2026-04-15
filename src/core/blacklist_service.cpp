/**
 * @file blacklist_service.cpp
 * @brief 黑名单服务封装类实现（简化版，无持久化）
 */

#include "blacklist_service.h"
#include "zip_utils.h"
#include "log_manager.h"
#include <iostream>
#include <chrono>

BlacklistService::BlacklistService(QueryMode mode)
    : status_(Status::UNINITIALIZED) {
    checker_ = std::make_unique<BlacklistChecker>(mode);
    const char* modeName[] = {"BLOOM_ONLY", "CARDINFO_ONLY", "BLOOM_AND_CARDINFO"};
    std::cout << "[BlacklistService] Created with mode: " << modeName[static_cast<int>(mode)] << std::endl;
    LOG_DEBUG("BlacklistService created with mode: %s", modeName[static_cast<int>(mode)]);
}

BlacklistService::~BlacklistService() {
    LOG_DEBUG("BlacklistService destroyed");
}

bool BlacklistService::initialize(const std::string& zipPath, QueryMode mode) {
    auto startTime = std::chrono::steady_clock::now();

    std::cout << "[BlacklistService] Loading from ZIP synchronously..." << std::endl;
    LOG_INFO("BlacklistService initialization starting...");
    LOG_INFO("ZIP path: %s", zipPath.c_str());

    {
        std::lock_guard<std::mutex> statusLock(statusMutex_);
        status_ = Status::LOADING;
    }

    size_t loaded = 0, invalid = 0;
    bool success = loadBlacklistFromCompressedFile(zipPath, *checker_, loaded, invalid);

    if (success) {
        std::cout << "[BlacklistService] Loaded " << loaded
                  << " records from ZIP" << std::endl;

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();

        LOG_INFO("BlacklistService initialization completed successfully");
        LOG_INFO("Loaded records: %zu, Invalid: %zu, Duration: %lld ms",
                 loaded, invalid, (long long)duration);

        {
            std::lock_guard<std::mutex> statusLock(statusMutex_);
            status_ = Status::READY;
        }
        return true;
    } else {
        setError("Failed to load from ZIP file");
        LOG_ERROR("BlacklistService initialization failed");

        {
            std::lock_guard<std::mutex> statusLock(statusMutex_);
            status_ = Status::ERROR;
        }
        return false;
    }
}

bool BlacklistService::update(const std::string& zipPath, QueryMode mode) {
    auto startTime = std::chrono::steady_clock::now();

    std::cout << "\n[BlacklistService] Updating blacklist..." << std::endl;
    LOG_INFO("BlacklistService update starting...");
    LOG_INFO("New ZIP path: %s", zipPath.c_str());

    {
        std::lock_guard<std::mutex> statusLock(statusMutex_);
        status_ = Status::LOADING;
    }

    checker_ = std::make_unique<BlacklistChecker>(mode);
    const char* modeName[] = {"BLOOM_ONLY", "CARDINFO_ONLY", "BLOOM_AND_CARDINFO"};
    std::cout << "[BlacklistService] Switched to mode: " << modeName[static_cast<int>(mode)] << std::endl;

    size_t loaded = 0, invalid = 0;
    bool success = loadBlacklistFromCompressedFile(zipPath, *checker_, loaded, invalid);

    if (success) {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();

        std::cout << "[BlacklistService] Update completed!" << std::endl;
        std::cout << "[BlacklistService] New records: " << loaded << std::endl;
        std::cout << "[BlacklistService] Duration: " << duration << " ms" << std::endl;

        LOG_INFO("BlacklistService update completed successfully");
        LOG_INFO("New records: %zu, Invalid: %zu, Duration: %lld ms",
                 loaded, invalid, (long long)duration);

        {
            std::lock_guard<std::mutex> statusLock(statusMutex_);
            status_ = Status::READY;
        }
        return true;
    } else {
        setError("Failed to update blacklist");
        LOG_ERROR("BlacklistService update failed");

        {
            std::lock_guard<std::mutex> statusLock(statusMutex_);
            status_ = Status::ERROR;
        }
        return false;
    }
}

bool BlacklistService::loadFromPersistFile(const std::string& persistPath) {
    auto startTime = std::chrono::steady_clock::now();

    std::cout << "[BlacklistService] Loading from persist file..." << std::endl;
    LOG_INFO("BlacklistService loading from persist file: %s", persistPath.c_str());

    {
        std::lock_guard<std::mutex> statusLock(statusMutex_);
        status_ = Status::LOADING;
    }

    bool success = checker_->loadFromPersistFile(persistPath);

    if (success) {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();

        std::cout << "[BlacklistService] Persist load completed in " << duration << " ms" << std::endl;
        LOG_INFO("Persist load completed in %lld ms", (long long)duration);

        {
            std::lock_guard<std::mutex> statusLock(statusMutex_);
            status_ = Status::READY;
        }
        return true;
    } else {
        setError("Failed to load from persist file");
        LOG_ERROR("Persist load failed");

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

QueryResult BlacklistService::checkCard(const std::string& cardId) {
    return checker_->checkCard(cardId);
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
    LOG_ERROR("BlacklistService error: %s", errorMsg.c_str());
}
