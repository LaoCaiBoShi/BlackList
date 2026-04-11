/**
 * @file persist_reader.cpp
 * @brief 持久化文件读取器实现（mmap 内存映射方式）
 *
 * 跨平台实现：Windows 使用 CreateFileMapping/MapViewOfFile
 *             Linux 使用 mmap/munmap
 */

#include "persist_reader.h"
#include <iostream>
#include <mutex>
#include <cstring>

// Windows 头文件
#ifdef _WIN32
    #include <windows.h>
#else
    // Linux 头文件
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
    #include <errno.h>
#endif

/**
 * @brief 构造函数
 */
PersistReader::PersistReader()
    : addr_(nullptr)
    , size_(0)
    , header_(nullptr)
    , indexTable_(nullptr)
    , isOpen_(false)
#ifdef _WIN32
    , hFile_(INVALID_HANDLE_VALUE)
    , hMap_(nullptr)
#else
    , fd_(-1)
#endif
{
}

/**
 * @brief 析构函数
 */
PersistReader::~PersistReader() {
    close();
}

/**
 * @brief 移动构造函数
 */
PersistReader::PersistReader(PersistReader&& other) noexcept
    : addr_(other.addr_)
    , size_(other.size_)
    , header_(other.header_)
    , indexTable_(other.indexTable_)
    , isOpen_(other.isOpen_.load())
#ifdef _WIN32
    , hFile_(other.hFile_)
    , hMap_(other.hMap_)
#else
    , fd_(other.fd_)
#endif
{
    other.addr_ = nullptr;
    other.header_ = nullptr;
    other.indexTable_ = nullptr;
    other.isOpen_ = false;
#ifdef _WIN32
    other.hFile_ = INVALID_HANDLE_VALUE;
    other.hMap_ = nullptr;
#else
    other.fd_ = -1;
#endif
}

/**
 * @brief 移动赋值运算符
 */
PersistReader& PersistReader::operator=(PersistReader&& other) noexcept {
    if (this != &other) {
        close();

        addr_ = other.addr_;
        size_ = other.size_;
        header_ = other.header_;
        indexTable_ = other.indexTable_;
        isOpen_ = other.isOpen_.load();
#ifdef _WIN32
        hFile_ = other.hFile_;
        hMap_ = other.hMap_;
#else
        fd_ = other.fd_;
#endif

        other.addr_ = nullptr;
        other.header_ = nullptr;
        other.indexTable_ = nullptr;
        other.isOpen_ = false;
#ifdef _WIN32
        other.hFile_ = INVALID_HANDLE_VALUE;
        other.hMap_ = nullptr;
#else
        other.fd_ = -1;
#endif
    }
    return *this;
}

/**
 * @brief 打开持久化文件
 */
bool PersistReader::open(const std::string& path) {
    // 如果已打开，先关闭
    if (isOpen_) {
        close();
    }

#ifdef _WIN32
    // Windows 实现
    // 打开文件
    hFile_ = CreateFileA(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hFile_ == INVALID_HANDLE_VALUE) {
        std::lock_guard<std::mutex> lock(errorMutex_);
        lastError_ = "Failed to open file: " + std::to_string(GetLastError());
        return false;
    }

    // 获取文件大小
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile_, &fileSize)) {
        std::lock_guard<std::mutex> lock(errorMutex_);
        lastError_ = "Failed to get file size: " + std::to_string(GetLastError());
        CloseHandle(hFile_);
        hFile_ = INVALID_HANDLE_VALUE;
        return false;
    }
    size_ = static_cast<size_t>(fileSize.QuadPart);

    // 创建文件映射对象
    hMap_ = CreateFileMappingA(
        hFile_,
        nullptr,
        PAGE_READONLY,
        0,  // 高位大小
        0,  // 低位大小，0表示整个文件
        nullptr
    );

    if (hMap_ == nullptr) {
        std::lock_guard<std::mutex> lock(errorMutex_);
        lastError_ = "Failed to create file mapping: " + std::to_string(GetLastError());
        CloseHandle(hFile_);
        hFile_ = INVALID_HANDLE_VALUE;
        return false;
    }

    // 映射到内存
    addr_ = MapViewOfFile(
        hMap_,
        FILE_MAP_READ,
        0,  // 高位偏移
        0,  // 低位偏移，0表示从头开始
        0   // 映射整个文件，0表示全部
    );

    if (addr_ == nullptr) {
        std::lock_guard<std::mutex> lock(errorMutex_);
        lastError_ = "Failed to map view of file: " + std::to_string(GetLastError());
        CloseHandle(hMap_);
        CloseHandle(hFile_);
        hMap_ = nullptr;
        hFile_ = INVALID_HANDLE_VALUE;
        return false;
    }

#else
    // Linux 实现
    // 打开文件
    fd_ = open(path.c_str(), O_RDONLY);
    if (fd_ == -1) {
        std::lock_guard<std::mutex> lock(errorMutex_);
        lastError_ = "Failed to open file: " + std::string(strerror(errno));
        return false;
    }

    // 获取文件大小
    struct stat sb;
    if (fstat(fd_, &sb) == -1) {
        std::lock_guard<std::mutex> lock(errorMutex_);
        lastError_ = "Failed to get file size: " + std::string(strerror(errno));
        close(fd_);
        fd_ = -1;
        return false;
    }
    size_ = static_cast<size_t>(sb.st_size);

    // 内存映射
    addr_ = mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0);
    if (addr_ == MAP_FAILED) {
        std::lock_guard<std::mutex> lock(errorMutex_);
        lastError_ = "Failed to mmap: " + std::string(strerror(errno));
        close(fd_);
        fd_ = -1;
        addr_ = nullptr;
        return false;
    }

    // 提示内核预读文件（可选，优化性能）
    madvise(addr_, size_, MADV_WILLNEED);
#endif

    // 初始化元数据指针
    header_ = static_cast<BlacklistChecker::PersistHeader*>(addr_);

    // 验证魔数
    if (std::memcmp(header_->magic, "BLCK", 4) != 0) {
        std::lock_guard<std::mutex> lock(errorMutex_);
        lastError_ = "Invalid persist file magic";
        close();
        return false;
    }

    // 验证版本
    if (header_->version != BlacklistChecker::PERSIST_FORMAT_VERSION) {
        std::lock_guard<std::mutex> lock(errorMutex_);
        lastError_ = "Persist file version mismatch: " +
                      std::to_string(header_->version) + " vs " +
                      std::to_string(BlacklistChecker::PERSIST_FORMAT_VERSION);
        close();
        return false;
    }

    // 设置索引表指针
    indexTable_ = reinterpret_cast<BlacklistChecker::PersistIndexEntry*>(
        static_cast<char*>(addr_) + sizeof(BlacklistChecker::PersistHeader)
    );

    isOpen_ = true;
    return true;
}

/**
 * @brief 关闭文件
 */
void PersistReader::close() {
    if (!isOpen_) {
        return;
    }

#ifdef _WIN32
    // Windows 实现
    if (addr_ != nullptr) {
        UnmapViewOfFile(addr_);
        addr_ = nullptr;
    }
    if (hMap_ != nullptr) {
        CloseHandle(hMap_);
        hMap_ = nullptr;
    }
    if (hFile_ != INVALID_HANDLE_VALUE) {
        CloseHandle(hFile_);
        hFile_ = INVALID_HANDLE_VALUE;
    }
#else
    // Linux 实现
    if (addr_ != nullptr) {
        munmap(addr_, size_);
        addr_ = nullptr;
    }
    if (fd_ != -1) {
        close(fd_);
        fd_ = -1;
    }
#endif

    header_ = nullptr;
    indexTable_ = nullptr;
    size_ = 0;
    isOpen_ = false;
}

/**
 * @brief 检查是否已打开
 */
bool PersistReader::isOpen() const {
    return isOpen_.load();
}

/**
 * @brief 获取 Header 指针
 */
const BlacklistChecker::PersistHeader* PersistReader::getHeader() const {
    return header_;
}

/**
 * @brief 获取总卡片数
 */
size_t PersistReader::getTotalCards() const {
    return header_ ? header_->totalCards : 0;
}

/**
 * @brief 获取省份数量
 */
size_t PersistReader::getPrefixCount() const {
    return header_ ? header_->prefixCount : 0;
}

/**
 * @brief 获取版本信息
 */
std::string PersistReader::getVersionInfo() const {
    if (!header_) {
        return "";
    }
    return std::string(header_->versionInfo, 6);
}

/**
 * @brief 获取错误信息
 */
std::string PersistReader::getLastError() const {
    std::lock_guard<std::mutex> lock(errorMutex_);
    return lastError_;
}

/**
 * @brief 静态方法：检查文件是否有效
 */
bool PersistReader::isValid(const std::string& path) {
    PersistReader reader;
    if (reader.open(path)) {
        reader.close();
        return true;
    }
    return false;
}

/**
 * @brief 查找省份索引
 */
const BlacklistChecker::PersistIndexEntry* PersistReader::findPrefixEntry(uint16_t prefix) const {
    if (!isOpen_ || !header_ || !indexTable_) {
        return nullptr;
    }

    // 线性查找（省份数量少，约31个）
    for (uint32_t i = 0; i < header_->prefixCount; ++i) {
        if (indexTable_[i].prefix == prefix) {
            return &indexTable_[i];
        }
    }
    return nullptr;
}

/**
 * @brief 在 mmap 内存中执行二分查找
 */
bool PersistReader::binarySearchInMmap(const BlacklistChecker::PersistIndexEntry* entry,
                                       const BlacklistChecker::CardInfo& target) const {
    if (!entry || !addr_) {
        return false;
    }

    // 根据 typeIdx 获取偏移和数量
    uint64_t offset = 0;
    uint32_t count = 0;

    // 计算数据起始地址
    char* dataStart = static_cast<char*>(addr_);

    // typeIdx 0 = type22
    if (entry->type0Count > 0) {
        offset = entry->type0Offset;
        count = entry->type0Count;
    } else if (entry->type1Count > 0) {
        offset = entry->type1Offset;
        count = entry->type1Count;
    } else if (entry->type2Count > 0) {
        offset = entry->type2Offset;
        count = entry->type2Count;
    } else {
        return false;
    }

    // 获取数据指针
    BlacklistChecker::CardInfo* cards =
        reinterpret_cast<BlacklistChecker::CardInfo*>(dataStart + offset);

    // 二分查找
    size_t left = 0;
    size_t right = count;

    while (left < right) {
        size_t mid = left + (right - left) / 2;
        if (cards[mid] < target) {
            left = mid + 1;
        } else if (target < cards[mid]) {
            right = mid;
        } else {
            return true;  // 找到
        }
    }

    return false;
}

/**
 * @brief 查询卡号
 */
bool PersistReader::query(const std::string& cardId) const {
    if (!isOpen_ || cardId.length() != 20) {
        return false;
    }

    // 解析卡号
    unsigned short prefix = 0;
    for (int i = 0; i < 4; ++i) {
        prefix = prefix * 10 + (cardId[i] - '0');
    }

    unsigned short year = 0;
    for (int i = 4; i < 6; ++i) {
        year = year * 10 + (cardId[i] - '0');
    }

    unsigned short week = 0;
    for (int i = 6; i < 8; ++i) {
        week = week * 10 + (cardId[i] - '0');
    }

    unsigned short cardType = 0;
    for (int i = 10; i < 12; ++i) {
        cardType = cardType * 10 + (cardId[i] - '0');
    }

    // 构建 CardInfo
    BlacklistChecker::CardInfo targetInfo(year, week, cardId.substr(12));

    // 查找省份
    const BlacklistChecker::PersistIndexEntry* entry = findPrefixEntry(prefix);
    if (!entry) {
        return false;
    }

    // 确定使用哪个 type 的数据
    // typeIdx 0 = type22, 1 = type23, 2 = 其他
    size_t typeIdx = (cardType == 22) ? 0 : (cardType == 23) ? 1 : 2;

    // 检查该类型是否有数据
    uint32_t count = 0;
    uint64_t offset = 0;

    switch (typeIdx) {
        case 0:
            count = entry->type0Count;
            offset = entry->type0Offset;
            break;
        case 1:
            count = entry->type1Count;
            offset = entry->type1Offset;
            break;
        case 2:
            count = entry->type2Count;
            offset = entry->type2Offset;
            break;
    }

    if (count == 0) {
        return false;
    }

    // 获取数据指针并二分查找
    char* dataStart = static_cast<char*>(addr_);
    BlacklistChecker::CardInfo* cards =
        reinterpret_cast<BlacklistChecker::CardInfo*>(dataStart + offset);

    size_t left = 0;
    size_t right = count;

    while (left < right) {
        size_t mid = left + (right - left) / 2;
        if (cards[mid] < targetInfo) {
            left = mid + 1;
        } else if (targetInfo < cards[mid]) {
            right = mid;
        } else {
            return true;
        }
    }

    return false;
}

/**
 * @brief 获取数据拷贝
 */
std::vector<BlacklistChecker::CardInfo> PersistReader::getDataCopy(uint16_t prefix, size_t typeIdx) {
    std::vector<BlacklistChecker::CardInfo> result;

    if (!isOpen_) {
        return result;
    }

    const BlacklistChecker::PersistIndexEntry* entry = findPrefixEntry(prefix);
    if (!entry) {
        return result;
    }

    uint32_t count = 0;
    uint64_t offset = 0;

    switch (typeIdx) {
        case 0:
            count = entry->type0Count;
            offset = entry->type0Offset;
            break;
        case 1:
            count = entry->type1Count;
            offset = entry->type1Offset;
            break;
        case 2:
            count = entry->type2Count;
            offset = entry->type2Offset;
            break;
        default:
            return result;
    }

    if (count == 0) {
        return result;
    }

    // 拷贝数据
    char* dataStart = static_cast<char*>(addr_);
    BlacklistChecker::CardInfo* cards =
        reinterpret_cast<BlacklistChecker::CardInfo*>(dataStart + offset);

    result.resize(count);
    std::memcpy(result.data(), cards, count * sizeof(BlacklistChecker::CardInfo));

    return result;
}