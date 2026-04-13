#include "memory_pool.h"
#include <iostream>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

std::atomic<size_t> MemoryPool::globalMemoryUsage_(0);

MemoryPool::MemoryPool(size_t blockSize, size_t blocksPerChunk)
    : blockSize(blockSize), blocksPerChunk(blocksPerChunk), currentUsed_(0) {
    if (blockSize == 0) {
        this->blockSize = DEFAULT_BLOCK_SIZE;
    }
    if (blocksPerChunk == 0) {
        this->blocksPerChunk = BLOCKS_PER_CHUNK;
    }
}

MemoryPool::~MemoryPool() {
    clear();
}

bool MemoryPool::allocateChunk() {
    size_t totalSize = blockSize * blocksPerChunk;

    if (globalMemoryUsage_.load() + totalSize > MAX_MEMORY_BYTES) {
        std::cerr << "[MemoryPool] Memory limit would be exceeded: "
                  << (globalMemoryUsage_.load() + totalSize) << " > "
                  << MAX_MEMORY_BYTES << std::endl;
        return false;
    }

    char* newBlock = static_cast<char*>(std::malloc(totalSize));
    if (!newBlock) {
        std::cerr << "[MemoryPool] Failed to allocate " << totalSize << " bytes" << std::endl;
        return false;
    }

    memset(newBlock, 0, totalSize);
    blocks.push_back(newBlock);
    globalMemoryUsage_.fetch_add(totalSize);

    for (size_t i = 0; i < blocksPerChunk; ++i) {
        freeBlocks.push_back(newBlock + i * blockSize);
    }

    return true;
}

void* MemoryPool::allocate() {
    std::lock_guard<std::mutex> lock(mutex);

    if (freeBlocks.empty()) {
        if (!allocateChunk()) {
            return nullptr;
        }
    }

    void* ptr = freeBlocks.back();
    freeBlocks.pop_back();
    currentUsed_.fetch_add(blockSize);

    return ptr;
}

void MemoryPool::deallocate(void* ptr) {
    if (!ptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex);

    freeBlocks.push_back(static_cast<char*>(ptr));
    currentUsed_.fetch_sub(blockSize);
}

void MemoryPool::clear() {
    std::lock_guard<std::mutex> lock(mutex);

    for (char* block : blocks) {
        std::free(block);
    }

    size_t totalSize = blocks.size() * blockSize * blocksPerChunk;
    globalMemoryUsage_.fetch_sub(totalSize);

    blocks.clear();
    freeBlocks.clear();
    currentUsed_.store(0);
}

size_t MemoryPool::getSize() const {
    std::lock_guard<std::mutex> lock(mutex);
    return blocks.size() * blockSize * blocksPerChunk;
}

size_t MemoryPool::getUsedSize() const {
    return currentUsed_.load();
}

size_t MemoryPool::getFreeBlockCount() const {
    std::lock_guard<std::mutex> lock(mutex);
    return freeBlocks.size();
}

size_t MemoryPool::getTotalBlockCount() const {
    std::lock_guard<std::mutex> lock(mutex);
    return blocks.size() * blocksPerChunk;
}

bool MemoryPool::isOverLimit() const {
    return globalMemoryUsage_.load() > MAX_MEMORY_BYTES;
}

size_t MemoryPool::getGlobalMemoryUsage() {
    return globalMemoryUsage_.load();
}

void MemoryPool::resetGlobalMemoryUsage() {
    globalMemoryUsage_.store(0);
}

void* safeAllocate(size_t size, MemoryPool& pool) {
    (void)size;
    void* ptr = pool.allocate();
    if (!ptr) {
        throw std::runtime_error("Memory allocation failed: pool returned nullptr");
    }
    return ptr;
}

void safeDeallocate(void* ptr, MemoryPool& pool) {
    pool.deallocate(ptr);
}

size_t getSystemAvailableMemoryMB() {
#ifdef _WIN32
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        return static_cast<size_t>(memInfo.ullAvailPhys / (1024 * 1024));
    }
#endif
    return 0;
}

bool hasEnoughMemory(size_t required) {
    size_t currentUsage = MemoryPool::getGlobalMemoryUsage();
    if (currentUsage + required > MAX_MEMORY_BYTES) {
        std::cerr << "[Memory] Not enough memory: need "
                  << required << " bytes, but would exceed limit" << std::endl;
        return false;
    }
    return true;
}