#ifndef SHARDED_BLOOM_FILTER_H
#define SHARDED_BLOOM_FILTER_H

#include <string>
#include <vector>
#include <array>
#include <atomic>
#include <mutex>
#include <cstddef>
#include <cmath>
#include <iostream>
#include <stdexcept>

class ShardedBloomFilter {
private:
    static const size_t NUM_SHARDS = 64;
    static const size_t DEFAULT_HASH_COUNT = 3;
    static const double SAFETY_FACTOR;

    struct Shard {
        std::vector<uint8_t> bits;
        std::atomic<size_t> elementCount{0};
    };

    std::array<Shard, NUM_SHARDS> shards_;
    size_t bitsPerShard_{0};
    size_t hashCount_{DEFAULT_HASH_COUNT};
    size_t totalElements_{0};

    size_t computeBitsPerShard(size_t totalElements, double falsePositiveRate);

public:
    explicit ShardedBloomFilter(size_t expectedElements = 30000000,
                                double falsePositiveRate = 0.00001);

    void add(const std::string& key);
    bool contains(const std::string& key) const;
    void clear();
    size_t size() const { return totalElements_; }
    size_t getMemoryUsage() const;
    size_t getShardLoad(size_t shardIdx) const;
    void printLoadDistribution() const;

    size_t getBitsPerShard() const { return bitsPerShard_; }
    size_t getHashCount() const { return hashCount_; }
    size_t getShardCount() const { return NUM_SHARDS; }
    size_t getTotalBits() const { return bitsPerShard_ * NUM_SHARDS; }
    size_t getElementCount() const { return totalElements_; }

private:
    inline size_t getShardIndex(const std::string& key) const;
    inline void computeHashes(const std::string& key, size_t& h1, size_t& h2) const;
};

#endif // SHARDED_BLOOM_FILTER_H