#include "shared_bloom_filter.h"

ShardedBloomFilter::ShardedBloomFilter() {
}

void ShardedBloomFilter::initialize(size_t shardCount, size_t expectedTotalElements,
                                    double falsePositiveRate) {
    numShards_ = shardCount;

    size_t elementsPerShard = (expectedTotalElements + shardCount - 1) / shardCount;
    bitsPerShard_ = computeBitsPerShard(elementsPerShard, falsePositiveRate);

    shards_.clear();
    shards_.reserve(shardCount);
    for (size_t i = 0; i < shardCount; ++i) {
        Shard shard;
        size_t bytes = bitsPerShard_ / 8 + 1;
        shard.bits.resize(bytes, 0);
        shard.elementCount.store(0, std::memory_order_relaxed);
        shards_.push_back(std::move(shard));
    }

    size_t totalBits = bitsPerShard_ * numShards_;
    size_t totalMB = totalBits / 8 / (1024 * 1024);
    std::cout << "[ShardedBloomFilter] Initialized: " << numShards_
              << " shards, " << bitsPerShard_ << " bits/shard, ~"
              << totalMB << " MB" << std::endl;
}

size_t ShardedBloomFilter::computeBitsPerShard(size_t elementsPerShard,
                                               double falsePositiveRate) {
    double n = static_cast<double>(elementsPerShard);
    double m = -n * std::log(falsePositiveRate) /
               (std::log(2) * std::log(2)) * SAFETY_FACTOR;
    return static_cast<size_t>(m);
}

inline size_t ShardedBloomFilter::getShardIndex(const std::string& key) const {
    size_t h = 0;
    for (size_t i = 0; i < std::min(key.length(), size_t(8)); ++i) {
        h = h * 31 + static_cast<unsigned char>(key[i]);
    }
    return h % numShards_;
}

inline void ShardedBloomFilter::computeHashes(const std::string& key,
                                              size_t& h1, size_t& h2) const {
    h1 = std::hash<std::string>{}(key);
    h2 = std::hash<std::string>{}(std::to_string(h1));
}

void ShardedBloomFilter::add(const std::string& key) {
    size_t shardIdx = getShardIndex(key);
    Shard& shard = shards_[shardIdx];

    size_t h1, h2;
    computeHashes(key, h1, h2);

    size_t bitCount = shard.bits.size() * 8;

    for (size_t i = 0; i < hashCount_; ++i) {
        size_t idx = (h1 + i * h2) % bitCount;
        shard.bits[idx / 8] |= (1 << (idx % 8));
    }

    shard.elementCount.fetch_add(1, std::memory_order_relaxed);
    totalElements_++;
}

bool ShardedBloomFilter::contains(const std::string& key) const {
    size_t shardIdx = getShardIndex(key);
    const Shard& shard = shards_[shardIdx];

    size_t h1, h2;
    computeHashes(key, h1, h2);

    size_t bitCount = shard.bits.size() * 8;

    for (size_t i = 0; i < hashCount_; ++i) {
        size_t idx = (h1 + i * h2) % bitCount;
        if (!(shard.bits[idx / 8] & (1 << (idx % 8)))) {
            return false;
        }
    }
    return true;
}

void ShardedBloomFilter::clear() {
    for (size_t i = 0; i < numShards_; ++i) {
        std::fill(shards_[i].bits.begin(), shards_[i].bits.end(), 0);
        shards_[i].elementCount.store(0, std::memory_order_relaxed);
    }
    totalElements_ = 0;
}

size_t ShardedBloomFilter::getMemoryUsage() const {
    size_t bytes = 0;
    for (const auto& shard : shards_) {
        bytes += shard.bits.capacity();
    }
    bytes += sizeof(*this);
    return bytes / (1024 * 1024);
}

size_t ShardedBloomFilter::getShardLoad(size_t shardIdx) const {
    if (shardIdx >= numShards_) return 0;
    return shards_[shardIdx].elementCount.load(std::memory_order_relaxed);
}

void ShardedBloomFilter::printLoadDistribution() const {
    std::cout << "[ShardedBloomFilter] Load distribution:" << std::endl;
    for (size_t i = 0; i < numShards_; ++i) {
        size_t load = shards_[i].elementCount.load(std::memory_order_relaxed);
        std::cout << "  Shard " << i << ": " << load << " elements" << std::endl;
    }
}
