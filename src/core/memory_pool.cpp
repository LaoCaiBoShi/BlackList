#include "memory_pool.h"
#include <cstdlib>

MemoryPool::MemoryPool(size_t blockSize, size_t blocksPerChunk) 
    : blockSize(blockSize), blocksPerChunk(blocksPerChunk) {
}

MemoryPool::~MemoryPool() {
    clear();
}

void* MemoryPool::allocate() {
    std::lock_guard<std::mutex> lock(mutex);
    
    if (freeBlocks.empty()) {
        char* newBlock = static_cast<char*>(std::malloc(blockSize * blocksPerChunk));
        if (!newBlock) {
            return nullptr;
        }
        
        blocks.push_back(newBlock);
        
        for (size_t i = 0; i < blocksPerChunk; ++i) {
            freeBlocks.push_back(newBlock + i * blockSize);
        }
    }
    
    void* ptr = freeBlocks.back();
    freeBlocks.pop_back();
    return ptr;
}

void MemoryPool::deallocate(void* ptr) {
    if (!ptr) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex);
    freeBlocks.push_back(static_cast<char*>(ptr));
}

void MemoryPool::clear() {
    std::lock_guard<std::mutex> lock(mutex);
    
    for (char* block : blocks) {
        std::free(block);
    }
    
    blocks.clear();
    freeBlocks.clear();
}

size_t MemoryPool::getSize() const {
    return blocks.size() * blockSize * blocksPerChunk;
}