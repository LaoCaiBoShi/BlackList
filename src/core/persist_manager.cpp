/**
 * @file persist_manager.cpp
 * @brief 黑名单持久化管理器实现
 */

#include "persist_manager.h"
#include "platform_utils.h"
#include "log_manager.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
#include <algorithm>

const char PersistManager::MAGIC[5] = "BLCK";

PersistManager::PersistManager() {
}

PersistManager::~PersistManager() {
}

std::string PersistManager::extractVersionFromFilename(const std::string& filename) {
    if (filename.empty()) {
        return "";
    }

    std::string name = filename;
    std::replace(name.begin(), name.end(), '\\', '/');

    size_t pos = name.find_last_of('/');
    if (pos != std::string::npos) {
        name = name.substr(pos + 1);
    }

    std::vector<std::string> parts;
    std::stringstream ss(name);
    std::string part;
    while (std::getline(ss, part, '_')) {
        parts.push_back(part);
    }

    if (parts.size() >= 3) {
        const std::string& dateStr = parts[2];
        if (dateStr.length() == 8 &&
            std::all_of(dateStr.begin(), dateStr.end(), ::isdigit)) {
            return dateStr;
        }
    }

    return "";
}

std::string PersistManager::getCacheDirectory() {
    using namespace platform;
    std::string exePath = FileSystem::instance().getExecutablePath();
    if (exePath.empty()) {
        return ".\\cache";
    }

    std::string exeDir = Path::getDirectory(exePath);
    return Path::join(exeDir, "cache");
}

std::string PersistManager::getCacheFilePath(const std::string& versionDate) {
    if (versionDate.empty()) {
        return "";
    }
    using namespace platform;
    std::string cacheDir = getCacheDirectory();
    return Path::join(cacheDir, "blacklist_cache_v" + versionDate + ".dat");
}

std::string PersistManager::findLatestCache() {
    using namespace platform;
    std::string cacheDir = getCacheDirectory();

    LOG_DEBUG("findLatestCache: cacheDir=%s", cacheDir.c_str());

    if (!FileSystem::instance().directoryExists(cacheDir)) {
        LOG_DEBUG("findLatestCache: directory does not exist");
        return "";
    }

    LOG_DEBUG("findLatestCache: directory exists");

    std::string latestPath;
    time_t latestTime = 0;

    auto files = FileSystem::instance().listFiles(cacheDir, "*.dat");
    LOG_DEBUG("findLatestCache: found %zu .dat files", files.size());

    for (const auto& file : files) {
        if (file.isDirectory) {
            LOG_DEBUG("findLatestCache: skipping directory %s", file.name.c_str());
            continue;
        }

        if (file.name.find("blacklist_cache_v") != 0) {
            LOG_DEBUG("findLatestCache: skipping %s (wrong prefix)", file.name.c_str());
            continue;
        }

        std::string fullPath = Path::join(cacheDir, file.name);
        time_t fileTime = FileSystem::instance().getFileModifiedTime(fullPath);
        LOG_DEBUG("findLatestCache: file=%s time=%ld", file.name.c_str(), (long)fileTime);

        if (fileTime > latestTime) {
            latestTime = fileTime;
            latestPath = fullPath;
            LOG_DEBUG("findLatestCache: new latest=%s", latestPath.c_str());
        }
    }

    LOG_DEBUG("findLatestCache: returning=%s", latestPath.c_str());
    return latestPath;
}

CacheLoadResult PersistManager::checkCacheAvailable(const std::string& versionDate) {
    using namespace platform;
    std::string cachePath;
    if (versionDate.empty()) {
        cachePath = findLatestCache();
    } else {
        cachePath = getCacheFilePath(versionDate);
    }

    if (cachePath.empty()) {
        return CacheLoadResult::NO_CACHE;
    }

    if (!FileSystem::instance().fileExists(cachePath)) {
        return CacheLoadResult::NO_CACHE;
    }

    std::ifstream ifs(cachePath, std::ios::binary);
    if (!ifs.is_open()) {
        return CacheLoadResult::FILE_IO_ERROR;
    }

    BlacklistChecker::PersistHeader header;
    ifs.read(reinterpret_cast<char*>(&header), sizeof(header));
    ifs.close();

    if (memcmp(header.magic, MAGIC, 4) != 0) {
        return CacheLoadResult::INVALID_HEADER;
    }

    return CacheLoadResult::SUCCESS;
}

bool PersistManager::getCacheInfo(const std::string& cachePath, CacheInfo& info) {
    using namespace platform;
    if (!FileSystem::instance().fileExists(cachePath)) {
        return false;
    }

    std::ifstream ifs(cachePath, std::ios::binary);
    if (!ifs.is_open()) {
        return false;
    }

    BlacklistChecker::PersistHeader header;
    ifs.read(reinterpret_cast<char*>(&header), sizeof(header));
    ifs.close();

    if (memcmp(header.magic, MAGIC, 4) != 0) {
        return false;
    }

    info.cachePath = cachePath;
    info.versionDate = std::string(header.versionInfo, 6);
    info.recordCount = header.totalCards;
    info.createdTime = header.createdTime;
    info.provinceCount = header.prefixCount;
    info.fileSize = FileSystem::instance().getFileSize(cachePath);
    info.zipMd5 = "";

    return true;
}

bool PersistManager::save(const std::string& zipPath, BlacklistChecker& checker) {
    using namespace platform;
    std::string versionDate = extractVersionFromFilename(zipPath);
    if (versionDate.empty()) {
        std::cerr << "[PersistManager] Cannot extract version from: " << zipPath << std::endl;
        LOG_ERROR("Cannot extract version from ZIP filename: %s", zipPath.c_str());
        return false;
    }

    std::string cacheDir = getCacheDirectory();
    std::cout << "[PersistManager] Cache directory: " << cacheDir << std::endl;
    LOG_INFO("Cache directory: %s", cacheDir.c_str());

    if (!createCacheDirectory()) {
        std::cerr << "[PersistManager] Failed to create cache directory" << std::endl;
        LOG_ERROR("Failed to create cache directory: %s", cacheDir.c_str());
        return false;
    }
    std::cout << "[PersistManager] Cache directory ready" << std::endl;
    LOG_INFO("Cache directory created/verified successfully");

    std::string cachePath = getCacheFilePath(versionDate);
    std::cout << "[PersistManager] Saving cache to: " << cachePath << std::endl;
    LOG_INFO("Saving blacklist cache to: %s", cachePath.c_str());

    bool success = checker.saveToPersistFile(cachePath);
    if (success) {
        std::cout << "[PersistManager] Cache saved successfully" << std::endl;
        LOG_INFO("Cache saved successfully: %s", cachePath.c_str());
    } else {
        std::cerr << "[PersistManager] Failed to save cache" << std::endl;
        LOG_ERROR("Failed to save cache: %s", cachePath.c_str());
    }

    return success;
}

CacheLoadResult PersistManager::load(const std::string& cachePath,
                                    BlacklistChecker& checker,
                                    QueryMode& mode) {
    using namespace platform;
    std::cout << "[PersistManager] Loading from cache: " << cachePath << std::endl;
    LOG_INFO("Loading blacklist from cache: %s", cachePath.c_str());

    auto startTime = std::chrono::steady_clock::now();

    if (!checker.loadFromPersistFile(cachePath)) {
        std::cerr << "[PersistManager] Failed to load from cache" << std::endl;
        LOG_ERROR("Failed to load from cache: %s", cachePath.c_str());
        return CacheLoadResult::DATA_CORRUPTED;
    }

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime).count();

    std::cout << "[PersistManager] Cache loaded in " << duration << " ms" << std::endl;
    LOG_INFO("Cache loaded in %lld ms", (long long)duration);

    return CacheLoadResult::SUCCESS;
}

bool PersistManager::isCacheUpToDate(const CacheInfo& cacheInfo, const std::string& zipPath) {
    std::string currentMd5 = calculateFileMd5(zipPath);
    if (currentMd5.empty()) {
        return false;
    }

    return cacheInfo.zipMd5 == currentMd5;
}

std::string PersistManager::calculateFileMd5(const std::string& filePath) {
#if defined(_WIN32) || defined(_WIN64)
    using namespace platform;
    if (!FileSystem::instance().fileExists(filePath)) {
        return "";
    }

    HANDLE hFile = CreateFileA(filePath.c_str(), GENERIC_READ,
                               FILE_SHARE_READ, NULL, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return "";
    }

    HCRYPTPROV hCryptProv;
    if (!CryptAcquireContext(&hCryptProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        CloseHandle(hFile);
        return "";
    }

    HCRYPTHASH hHash;
    if (!CryptCreateHash(hCryptProv, CALG_MD5, 0, 0, &hHash)) {
        CryptReleaseContext(hCryptProv, 0);
        CloseHandle(hFile);
        return "";
    }

    const DWORD bufferSize = 8192;
    unsigned char buffer[bufferSize];
    DWORD bytesRead = 0;

    while (ReadFile(hFile, buffer, bufferSize, &bytesRead, NULL) && bytesRead > 0) {
        CryptHashData(hHash, buffer, bytesRead, 0);
    }

    unsigned char hash[16];
    DWORD hashSize = 16;
    if (!CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashSize, 0)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hCryptProv, 0);
        CloseHandle(hFile);
        return "";
    }

    CryptDestroyHash(hHash);
    CryptReleaseContext(hCryptProv, 0);
    CloseHandle(hFile);

    char md5Str[33];
    for (int i = 0; i < 16; i++) {
        sprintf(md5Str + i * 2, "%02x", hash[i]);
    }
    md5Str[32] = '\0';

    return std::string(md5Str);
#else
    return "";
#endif
}

bool PersistManager::createCacheDirectory() {
    using namespace platform;
    std::string cacheDir = getCacheDirectory();

    if (FileSystem::instance().directoryExists(cacheDir)) {
        std::cout << "[PersistManager] Cache directory already exists: " << cacheDir << std::endl;
        LOG_INFO("Cache directory already exists: %s", cacheDir.c_str());
        return true;
    }

    if (FileSystem::instance().createDirectory(cacheDir)) {
        std::cout << "[PersistManager] Cache directory created: " << cacheDir << std::endl;
        LOG_INFO("Cache directory created: %s", cacheDir.c_str());
        return true;
    }

    std::cerr << "[PersistManager] Failed to create cache directory: " << cacheDir << std::endl;
    std::cerr << "[PersistManager] Error: " << FileSystem::instance().getLastErrorString() << std::endl;
    LOG_ERROR("Failed to create cache directory: %s, error: %s",
              cacheDir.c_str(), FileSystem::instance().getLastErrorString().c_str());
    return false;
}

std::string extractDirectory(const std::string& filePath) {
    using namespace platform;
    return Path::getDirectory(filePath);
}
