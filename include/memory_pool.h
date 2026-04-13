#ifndef MEMORY_POOL_H
#define MEMORY_POOL_H

#include <vector>
#include <mutex>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <stdexcept>

/**
 * @brief 内存限制常量（1000MB）
 */
constexpr size_t MAX_MEMORY_BYTES = 1000ULL * 1024 * 1024;

/**
 * @brief 内存块默认大小（64KB）
 */
constexpr size_t DEFAULT_BLOCK_SIZE = 64 * 1024;

/**
 * @brief 每次分配的块数
 */
constexpr size_t BLOCKS_PER_CHUNK = 100;

/**
 * @brief 内存池类，用于管理内存分配和释放
 *
 * 设计目标：
 * 1. 减少内存分配次数，提高性能
 * 2. 控制内存使用量，防止超过限制
 * 3. 追踪内存使用情况，便于监控
 */
class MemoryPool {
public:
    /**
     * @brief 构造函数
     * @param blockSize 每个内存块的大小（字节）
     * @param blocksPerChunk 每次分配的块数
     */
    MemoryPool(size_t blockSize = DEFAULT_BLOCK_SIZE, size_t blocksPerChunk = BLOCKS_PER_CHUNK);

    /**
     * @brief 析构函数
     */
    ~MemoryPool();

    /**
     * @brief 分配内存
     * @return 分配的内存指针，失败返回nullptr
     */
    void* allocate();

    /**
     * @brief 释放内存
     * @param ptr 要释放的内存指针
     */
    void deallocate(void* ptr);

    /**
     * @brief 清理内存池，释放所有内存
     */
    void clear();

    /**
     * @brief 获取当前内存池大小（字节）
     * @return 内存池大小
     */
    size_t getSize() const;

    /**
     * @brief 获取当前使用的内存大小（字节）
     * @return 使用的内存大小
     */
    size_t getUsedSize() const;

    /**
     * @brief 获取可用内存块数量
     * @return 可用内存块数量
     */
    size_t getFreeBlockCount() const;

    /**
     * @brief 获取总分配的块数
     * @return 总块数
     */
    size_t getTotalBlockCount() const;

    /**
     * @brief 检查内存使用是否超过限制
     * @return 是否超过限制
     */
    bool isOverLimit() const;

    /**
     * @brief 获取当前全局内存使用量
     * @return 全局内存使用量（字节）
     */
    static size_t getGlobalMemoryUsage();

    /**
     * @brief 重置全局内存使用量（用于新任务）
     */
    static void resetGlobalMemoryUsage();

private:
    std::vector<char*> blocks;         // 所有分配的内存块
    std::vector<char*> freeBlocks;      // 空闲内存块
    size_t blockSize;                  // 单个块大小
    size_t blocksPerChunk;             // 每次分配的块数
    mutable std::mutex mutex;           // 互斥锁

    std::atomic<size_t> currentUsed_;   // 当前使用的内存量

    static std::atomic<size_t> globalMemoryUsage_;  // 全局内存使用量

    /**
     * @brief 分配新的内存块
     * @return 是否分配成功
     */
    bool allocateChunk();
};

/**
 * @brief 安全分配内存，带内存限制检查
 * @param size 需要的内存大小
 * @param pool 内存池
 * @return 分配的内存指针，失败抛出异常
 */
void* safeAllocate(size_t size, MemoryPool& pool);

/**
 * @brief 安全释放内存
 * @param ptr 内存指针
 * @param pool 内存池
 */
void safeDeallocate(void* ptr, MemoryPool& pool);

/**
 * @brief 获取系统可用内存（MB）
 * @return 可用内存大小
 */
size_t getSystemAvailableMemoryMB();

/**
 * @brief 检查是否有足够的内存完成操作
 * @param required 需要的内存大小（字节）
 * @return 是否有足够的内存
 */
bool hasEnoughMemory(size_t required);

#endif // MEMORY_POOL_H