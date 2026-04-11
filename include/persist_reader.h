/**
 * @file persist_reader.h
 * @brief 持久化文件读取器（mmap 内存映射方式）
 *
 * 使用 mmap 实现持久化文件的零拷贝读取，提高加载速度和内存效率
 * 支持跨平台（Windows/Linux）
 */

#ifndef PERSIST_READER_H
#define PERSIST_READER_H

#include "blacklist_checker.h"
#include <string>
#include <atomic>
#include <memory>

class PersistReader {
public:
    /**
     * @brief 构造函数
     */
    PersistReader();

    /**
     * @brief 析构函数，自动关闭文件
     */
    ~PersistReader();

    /**
     * @brief 禁用拷贝构造函数
     */
    PersistReader(const PersistReader&) = delete;

    /**
     * @brief 禁用赋值运算符
     */
    PersistReader& operator=(const PersistReader&) = delete;

    /**
     * @brief 移动构造函数
     */
    PersistReader(PersistReader&& other) noexcept;

    /**
     * @brief 移动赋值运算符
     */
    PersistReader& operator=(PersistReader&& other) noexcept;

    /**
     * @brief 打开持久化文件（mmap 方式）
     *
     * 使用内存映射方式打开文件，实现零拷贝读取
     *
     * @param path 持久化文件路径
     * @return 是否成功打开
     */
    bool open(const std::string& path);

    /**
     * @brief 关闭文件（munmap）
     *
     * 释放 mmap 映射，关闭文件描述符
     */
    void close();

    /**
     * @brief 检查是否已打开
     * @return 是否已打开
     */
    bool isOpen() const;

    /**
     * @brief 查询卡号是否在持久化数据中（mmap 直接查询）
     *
     * 直接在 mmap 内存中进行二分查找，无需拷贝数据
     *
     * @param cardId 完整卡号（20位）
     * @return 是否可能在黑名单中（布隆过滤器语义）
     */
    bool query(const std::string& cardId) const;

    /**
     * @brief 获取数据拷贝（用于 BlacklistChecker 内部存储）
     *
     * @param prefix 省份代码
     * @param typeIdx 类型索引（0=type22, 1=type23, 2=其他）
     * @return CardInfo 数组拷贝
     */
    std::vector<BlacklistChecker::CardInfo> getDataCopy(uint16_t prefix, size_t typeIdx);

    /**
     * @brief 获取 Header 指针（mmap 直接访问）
     * @return Header 指针，失败返回 nullptr
     */
    const BlacklistChecker::PersistHeader* getHeader() const;

    /**
     * @brief 获取总卡片数
     * @return 总卡片数
     */
    size_t getTotalCards() const;

    /**
     * @brief 获取省份数量
     * @return 省份数量
     */
    size_t getPrefixCount() const;

    /**
     * @brief 获取版本信息
     * @return 版本信息字符串
     */
    std::string getVersionInfo() const;

    /**
     * @brief 检查文件是否有效（静态方法，无需实例化）
     *
     * @param path 持久化文件路径
     * @return 是否有效
     */
    static bool isValid(const std::string& path);

    /**
     * @brief 获取错误信息
     * @return 错误描述
     */
    std::string getLastError() const;

private:
    /**
     * @brief 查找省份索引
     * @param prefix 省份代码
     * @return 索引条目指针，未找到返回 nullptr
     */
    const BlacklistChecker::PersistIndexEntry* findPrefixEntry(uint16_t prefix) const;

    /**
     * @brief 在 mmap 内存中执行二分查找
     */
    bool binarySearchInMmap(const BlacklistChecker::PersistIndexEntry* entry,
                           const BlacklistChecker::CardInfo& target) const;

    // mmap 相关成员
#ifdef _WIN32
    void* hFile_;    // HANDLE
    void* hMap_;    // HANDLE
#else
    int fd_;        // 文件描述符
#endif
    void* addr_;    // mmap 映射地址
    size_t size_;   // 文件大小

    // 元数据指针（mmap 内存中的数据）
    BlacklistChecker::PersistHeader* header_;
    BlacklistChecker::PersistIndexEntry* indexTable_;

    // 状态
    std::atomic<bool> isOpen_;

    // 错误信息
    std::string lastError_;
    mutable std::mutex errorMutex_;
};

#endif // PERSIST_READER_H