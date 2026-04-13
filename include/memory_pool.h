#ifndef MEMORY_POOL_H
#define MEMORY_POOL_H

#include <vector>
#include <mutex>

/**
 * @brief 内存池类，用于管理内存分配和释放
 */
class MemoryPool {
private:
    std::vector<char*> blocks;
    std::vector<char*> freeBlocks;
    size_t blockSize;
    size_t blocksPerChunk;
    std::mutex mutex;

public:
    /**
     * @brief 构造函数
     * @param blockSize 每个内存块的大小
     * @param blocksPerChunk 每次分配的块数
     */
    MemoryPool(size_t blockSize = 1024, size_t blocksPerChunk = 100);

    /**
     * @brief 析构函数
     */
    ~MemoryPool();

    /**
     * @brief 分配内存
     * @return 分配的内存指针
     */
    void* allocate();

    /**
     * @brief 释放内存
     * @param ptr 要释放的内存指针
     */
    void deallocate(void* ptr);

    /**
     * @brief 清理内存池
     */
    void clear();

    /**
     * @brief 获取当前内存池大小
     * @return 内存池大小（字节）
     */
    size_t getSize() const;
};

#endif // MEMORY_POOL_H