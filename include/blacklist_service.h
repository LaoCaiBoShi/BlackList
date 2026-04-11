/**
 * @file blacklist_service.h
 * @brief 黑名单服务封装类
 *
 * 提供黑名单的加载、查询、更新等功能的统一封装
 * 支持：
 * - 持久化文件快速恢复
 * - 后台异步加载
 * - 热更新
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
    /**
     * @brief 服务状态枚举
     */
    enum class Status {
        UNINITIALIZED,  ///< 未初始化
        LOADING,        ///< 正在从ZIP加载数据
        READY,          ///< 就绪，可接受查询
        UPDATING,       ///< 正在更新数据
        ERROR           ///< 错误状态
    };

    /**
     * @brief 构造函数
     */
    BlacklistService();

    /**
     * @brief 析构函数
     */
    ~BlacklistService();

    /**
     * @brief 初始化服务
     *
     * 优先尝试从持久化文件恢复，如果成功则立即进入READY状态
     * 同时在后台启动ZIP文件的加载流程，加载完成后自动更新数据
     *
     * @param zipPath ZIP压缩文件路径
     * @param persistPath 持久化文件路径
     * @return 初始化是否成功（持久化恢复成功即为成功）
     */
    bool initialize(const std::string& zipPath, const std::string& persistPath);

    /**
     * @brief 查询卡号是否在黑名单中
     *
     * @param cardId 卡号（20位字符串）
     * @return 是否在黑名单中
     */
    bool isBlacklisted(const std::string& cardId);

    /**
     * @brief 触发数据更新
     *
     * 在后台线程中重新加载ZIP文件数据，加载完成后自动切换
     *
     * @param zipPath 新的ZIP压缩文件路径
     */
    void triggerUpdate(const std::string& zipPath);

    /**
     * @brief 同步更新数据（阻塞等待）
     *
     * @param zipPath ZIP压缩文件路径
     * @return 更新是否成功
     */
    bool syncUpdate(const std::string& zipPath);

    /**
     * @brief 获取当前服务状态
     * @return 服务状态
     */
    Status getStatus() const;

    /**
     * @brief 获取状态描述字符串
     * @return 状态描述
     */
    std::string getStatusString() const;

    /**
     * @brief 获取当前版本信息
     * @return 版本信息字符串
     */
    std::string getVersionInfo() const;

    /**
     * @brief 获取已加载的黑名单数量
     * @return 黑名单数量
     */
    size_t getBlacklistSize() const;

    /**
     * @brief 检查持久化文件是否存在且有效
     * @param persistPath 持久化文件路径
     * @return 是否有效
     */
    bool isPersistFileValid(const std::string& persistPath) const;

    /**
     * @brief 等待服务进入就绪状态
     *
     * @param timeoutMs 超时毫秒数，0表示无限等待
     * @return 是否成功进入就绪状态
     */
    bool waitForReady(int timeoutMs = 0);

    /**
     * @brief 获取错误信息
     * @return 错误描述字符串
     */
    std::string getLastError() const;

    /**
     * @brief 保存当前数据到持久化文件
     *
     * @param persistPath 持久化文件路径
     * @return 保存是否成功
     */
    bool savePersistFile(const std::string& persistPath);

    /**
     * @brief 获取服务是否正在运行（就绪或更新中）
     * @return 是否正在运行
     */
    bool isRunning() const;

private:
    /**
     * @brief 后台加载线程函数
     */
    void backgroundLoadingThread(const std::string& zipPath);

    /**
     * @brief 设置错误状态
     */
    void setError(const std::string& errorMsg);

    // 核心黑名单检查器（使用指针避免mutex复制问题）
    std::unique_ptr<BlacklistChecker> checker_;

    // 当前状态
    std::atomic<Status> status_;

    // 错误信息
    std::string lastError_;
    mutable std::mutex errorMutex_;

    // 更新线程
    std::unique_ptr<std::thread> updateThread_;

    // 状态变更通知
    std::condition_variable statusCV_;
    mutable std::mutex statusMutex_;

    // ZIP路径和数据交换锁
    std::string currentZipPath_;
    std::mutex pathMutex_;
};

#endif // BLACKLIST_SERVICE_H