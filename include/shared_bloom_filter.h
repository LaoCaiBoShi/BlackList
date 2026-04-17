#ifndef SHARDED_BLOOM_FILTER_H
#define SHARDED_BLOOM_FILTER_H

#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <cstddef>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <memory>

class ShardedBloomFilter {
private:
    static constexpr size_t DEFAULT_HASH_COUNT = 3;
    static constexpr double SAFETY_FACTOR = 1.3;

    struct Shard {
        std::vector<uint8_t> bits;
        std::atomic<size_t> elementCount{0};

        Shard() = default;
        Shard(Shard&& other) noexcept
            : bits(std::move(other.bits))
            , elementCount(other.elementCount.load()) {}
        Shard& operator=(Shard&& other) noexcept {
            bits = std::move(other.bits);
            elementCount.store(other.elementCount.load());
            return *this;
        }
        Shard(const Shard&) = delete;
        Shard& operator=(const Shard&) = delete;
    };

    std::vector<Shard> shards_;
    size_t numShards_{0};
    size_t bitsPerShard_{0};
    size_t hashCount_{DEFAULT_HASH_COUNT};
    size_t totalElements_{0};

    size_t computeBitsPerShard(size_t totalElements, double falsePositiveRate);

public:
    ShardedBloomFilter();

    void initialize(size_t shardCount, size_t expectedTotalElements,
                    double falsePositiveRate = 0.000001);

    void add(const std::string& key);
    bool contains(const std::string& key) const;
    void clear();
    size_t size() const { return totalElements_; }
    size_t getMemoryUsage() const;
    size_t getShardLoad(size_t shardIdx) const;
    void printLoadDistribution() const;

    size_t getBitsPerShard() const { return bitsPerShard_; }
    size_t getHashCount() const { return hashCount_; }
    size_t getShardCount() const { return numShards_; }
    size_t getTotalBits() const { return bitsPerShard_ * numShards_; }
    size_t getElementCount() const { return totalElements_; }

private:
    inline size_t getShardIndex(const std::string& key) const;
    inline void computeHashes(const std::string& key, size_t& h1, size_t& h2) const;
};

#endif // SHARDED_BLOOM_FILTER_H
