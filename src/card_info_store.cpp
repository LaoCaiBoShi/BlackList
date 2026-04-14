#include "card_info_store.h"
#include <algorithm>
#include <iostream>

CardInfoStore::CardInfoStore() {
}

void CardInfoStore::add(uint8_t provinceCode, size_t typeIdx, const CardInfo& card) {
    size_t shardIdx = getShardIndex(provinceCode);
    std::lock_guard<std::mutex> lock(shards_[shardIdx].mutex);
    shards_[shardIdx].cards[typeIdx].push_back(card);
}

void CardInfoStore::addBatch(uint8_t provinceCode, size_t typeIdx, const std::vector<CardInfo>& cards) {
    size_t shardIdx = getShardIndex(provinceCode);
    std::lock_guard<std::mutex> lock(shards_[shardIdx].mutex);
    auto& vec = shards_[shardIdx].cards[typeIdx];
    vec.insert(vec.end(), cards.begin(), cards.end());
}

bool CardInfoStore::binarySearch(uint8_t provinceCode, size_t typeIdx, const CardInfo& target) const {
    size_t shardIdx = getShardIndex(provinceCode);
    const std::lock_guard<std::mutex> lock(shards_[shardIdx].mutex);
    const auto& vec = shards_[shardIdx].cards[typeIdx];
    return std::binary_search(vec.begin(), vec.end(), target);
}

void CardInfoStore::sortProvince(uint8_t provinceCode) {
    size_t shardIdx = getShardIndex(provinceCode);
    std::lock_guard<std::mutex> lock(shards_[shardIdx].mutex);
    for (size_t i = 0; i < MAX_TYPE_COUNT; ++i) {
        auto& vec = shards_[shardIdx].cards[i];
        std::sort(vec.begin(), vec.end());
    }
}

void CardInfoStore::sortAll() {
    for (size_t shardIdx = 0; shardIdx < MAX_PROVINCE_CODE; ++shardIdx) {
        for (size_t i = 0; i < MAX_TYPE_COUNT; ++i) {
            std::lock_guard<std::mutex> lock(shards_[shardIdx].mutex);
            auto& vec = shards_[shardIdx].cards[i];
            std::sort(vec.begin(), vec.end());
        }
    }
}

size_t CardInfoStore::size() const {
    size_t total = 0;
    for (size_t shardIdx = 0; shardIdx < MAX_PROVINCE_CODE; ++shardIdx) {
        for (size_t i = 0; i < MAX_TYPE_COUNT; ++i) {
            total += shards_[shardIdx].cards[i].size();
        }
    }
    return total;
}

size_t CardInfoStore::getMemoryUsage() const {
    size_t bytes = sizeof(*this);
    for (size_t shardIdx = 0; shardIdx < MAX_PROVINCE_CODE; ++shardIdx) {
        for (size_t i = 0; i < MAX_TYPE_COUNT; ++i) {
            bytes += shards_[shardIdx].cards[i].capacity() * sizeof(CardInfo);
        }
    }
    return bytes / (1024 * 1024);
}

size_t CardInfoStore::getProvinceCount() const {
    size_t count = 0;
    for (size_t shardIdx = 0; shardIdx < MAX_PROVINCE_CODE; ++shardIdx) {
        for (size_t i = 0; i < MAX_TYPE_COUNT; ++i) {
            if (!shards_[shardIdx].cards[i].empty()) {
                count++;
                break;
            }
        }
    }
    return count;
}

size_t CardInfoStore::getShardSize(uint8_t provinceCode, size_t typeIdx) const {
    size_t shardIdx = getShardIndex(provinceCode);
    return shards_[shardIdx].cards[typeIdx].size();
}

void CardInfoStore::clear() {
    for (size_t shardIdx = 0; shardIdx < MAX_PROVINCE_CODE; ++shardIdx) {
        for (size_t i = 0; i < MAX_TYPE_COUNT; ++i) {
            std::lock_guard<std::mutex> lock(shards_[shardIdx].mutex);
            shards_[shardIdx].cards[i].clear();
        }
    }
}

void CardInfoStore::reserveProvinceCapacity(uint8_t provinceCode, size_t capacity) {
    size_t shardIdx = getShardIndex(provinceCode);
    std::lock_guard<std::mutex> lock(shards_[shardIdx].mutex);
    for (size_t i = 0; i < MAX_TYPE_COUNT; ++i) {
        shards_[shardIdx].cards[i].reserve(capacity / MAX_TYPE_COUNT + 100);
    }
}