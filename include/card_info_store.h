#ifndef CARD_INFO_STORE_H
#define CARD_INFO_STORE_H

#include <array>
#include <vector>
#include <mutex>
#include <cstdint>
#include <string>

constexpr size_t MAX_PROVINCE_CODE = 100;
constexpr size_t MAX_TYPE_COUNT = 3;

struct CardInfo {
    uint16_t year_week;     // 4位BCD: 年份(5-6) + 星期(7-8)
    uint64_t innerId;       // 10位数字: 序号(11-20)

    CardInfo() : year_week(0), innerId(0) {}
    CardInfo(uint16_t yw, uint64_t id) : year_week(yw), innerId(id) {}

    bool operator<(const CardInfo& other) const {
        if (year_week != other.year_week) {
            return year_week < other.year_week;
        }
        return innerId < other.innerId;
    }

    bool operator==(const CardInfo& other) const {
        return year_week == other.year_week && innerId == other.innerId;
    }
};

class CardInfoStore {
private:
    struct ProvinceShard {
        std::array<std::vector<CardInfo>, MAX_TYPE_COUNT> cards;
        mutable std::mutex mutex;
    };

    std::array<ProvinceShard, MAX_PROVINCE_CODE> shards_;

public:
    CardInfoStore();

    void add(uint8_t provinceCode, size_t typeIdx, const CardInfo& card);
    void addBatch(uint8_t provinceCode, size_t typeIdx, const std::vector<CardInfo>& cards);

    bool binarySearch(uint8_t provinceCode, size_t typeIdx, const CardInfo& target) const;

    void sortProvince(uint8_t provinceCode);
    void sortAll();

    size_t size() const;
    size_t getMemoryUsage() const;
    size_t getProvinceCount() const;
    size_t getShardSize(uint8_t provinceCode, size_t typeIdx) const;

    void clear();
    void reserveProvinceCapacity(uint8_t provinceCode, size_t capacity);

private:
    static size_t getShardIndex(uint8_t provinceCode) {
        return provinceCode < MAX_PROVINCE_CODE ? provinceCode : 0;
    }
};

#endif // CARD_INFO_STORE_H