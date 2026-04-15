/**
 * @file blacklist_service.h
 * @brief 黑名单服务封装类（简化版，无持久化）
 *
 * 提供黑名单的加载、查询等功能的统一封装
 */

#ifndef BLACKLIST_SERVICE_H
#define BLACKLIST_SERVICE_H

#include "blacklist_checker.h"
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>

class BlacklistService {
public:
    enum class Status {
        UNINITIALIZED,
        LOADING,
        READY,
        ERROR
    };

    explicit BlacklistService(QueryMode mode = QueryMode::CARDINFO_ONLY);
    ~BlacklistService();

    bool initialize(const std::string& zipPath, QueryMode mode = QueryMode::CARDINFO_ONLY);
    bool update(const std::string& zipPath, QueryMode mode);
    bool isBlacklisted(const std::string& cardId);
    QueryResult checkCard(const std::string& cardId);
    Status getStatus() const;
    std::string getStatusString() const;
    std::string getVersionInfo() const;
    size_t getBlacklistSize() const;
    bool waitForReady(int timeoutMs = 0);
    std::string getLastError() const;
    bool isRunning() const;

private:
    void setError(const std::string& errorMsg);

    std::unique_ptr<BlacklistChecker> checker_;
    std::atomic<Status> status_;
    std::string lastError_;
    mutable std::mutex errorMutex_;
    std::condition_variable statusCV_;
    mutable std::mutex statusMutex_;
};

#endif
