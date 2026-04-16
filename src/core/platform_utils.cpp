/**
 * @file platform_utils.cpp
 * @brief 跨平台文件系统工具类实现
 */

#include "platform_utils.h"
#include <algorithm>
#include <iostream>

namespace platform {

FileSystem& FileSystem::instance() {
    static FileSystem instance;
    return instance;
}

std::string FileSystem::getExecutablePath() {
#if defined(_WIN32) || defined(_WIN64)
    char path[MAX_PATH];
    if (GetModuleFileNameA(NULL, path, MAX_PATH) > 0) {
        return std::string(path);
    }
    return "";
#else
    return getExecutablePathLinux();
#endif
}

#if !defined(_WIN32) && !defined(_WIN64)
std::string FileSystem::getExecutablePathLinux() {
    char result[1024];
    ssize_t count = readlink("/proc/self/exe", result, sizeof(result) - 1);
    if (count != -1) {
        result[count] = '\0';
        return std::string(result);
    }
    return "";
}

std::vector<FileInfo> FileSystem::listFilesLinux(const std::string& path,
                                                 const std::string& pattern) {
    std::vector<FileInfo> result;
    DIR* dir = opendir(path.c_str());
    if (!dir) {
        return result;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;

        if (name == "." || name == "..") {
            continue;
        }

        if (!pattern.empty() && pattern != "*" && name.find(pattern) == std::string::npos) {
            continue;
        }

        FileInfo info;
        info.name = name;
        info.isDirectory = (entry->d_type == DT_DIR);

        std::string fullPath = Path::join(path, name);
        struct stat st;
        if (stat(fullPath.c_str(), &st) == 0) {
            info.size = st.st_size;
            info.modifiedTime = st.st_mtime;
        } else {
            info.size = 0;
            info.modifiedTime = 0;
        }

        result.push_back(info);
    }

    closedir(dir);
    return result;
}
#endif

bool FileSystem::createDirectory(const std::string& path) {
#if defined(_WIN32) || defined(_WIN64)
    if (CreateDirectoryA(path.c_str(), NULL)) {
        return true;
    }
    DWORD err = GetLastError();
    return err == ERROR_ALREADY_EXISTS;
#else
    if (mkdir(path.c_str(), 0755) == 0) {
        return true;
    }
    return errno == EEXIST;
#endif
}

bool FileSystem::directoryExists(const std::string& path) {
#if defined(_WIN32) || defined(_WIN64)
    DWORD attr = GetFileAttributesA(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

std::vector<FileInfo> FileSystem::listFiles(const std::string& path,
                                            const std::string& pattern) {
#if defined(_WIN32) || defined(_WIN64)
    std::vector<FileInfo> result;
    WIN32_FIND_DATAA findData;
    std::string searchPattern = Path::join(path, pattern.empty() ? "*" : pattern);

    HANDLE hFind = FindFirstFileA(searchPattern.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        return result;
    }

    do {
        if (strcmp(findData.cFileName, ".") == 0 ||
            strcmp(findData.cFileName, "..") == 0) {
            continue;
        }

        if (!pattern.empty() && pattern != "*") {
            std::string name(findData.cFileName);
            if (name.find(pattern) == std::string::npos) {
                continue;
            }
        }

        FileInfo info;
        info.name = findData.cFileName;
        info.isDirectory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        info.size = (static_cast<uint64_t>(findData.nFileSizeHigh) << 32) |
                    findData.nFileSizeLow;
        info.modifiedTime = 0;

        result.push_back(info);
    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);
    return result;
#else
    return listFilesLinux(path, pattern);
#endif
}

bool FileSystem::fileExists(const std::string& path) {
#if defined(_WIN32) || defined(_WIN64)
    DWORD attr = GetFileAttributesA(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
#endif
}

uint64_t FileSystem::getFileSize(const std::string& path) {
#if defined(_WIN32) || defined(_WIN64)
    WIN32_FILE_ATTRIBUTE_DATA attr;
    if (GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &attr)) {
        return (static_cast<uint64_t>(attr.nFileSizeHigh) << 32) |
               attr.nFileSizeLow;
    }
    return 0;
#else
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return st.st_size;
    }
    return 0;
#endif
}

time_t FileSystem::getFileModifiedTime(const std::string& path) {
#if defined(_WIN32) || defined(_WIN64)
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return st.st_mtime;
    }
    return 0;
#else
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return st.st_mtime;
    }
    return 0;
#endif
}

std::string FileSystem::getLastErrorString() {
#if defined(_WIN32) || defined(_WIN64)
    DWORD err = GetLastError();
    char* msgBuffer = nullptr;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                  NULL, err, 0, (char*)&msgBuffer, 0, NULL);
    std::string result = msgBuffer ? msgBuffer : "Unknown error";
    LocalFree(msgBuffer);
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
        result.pop_back();
    }
    return result;
#else
    return std::string(strerror(errno));
#endif
}

std::string FileSystem::getLastErrorCode() {
#if defined(_WIN32) || defined(_WIN64)
    return std::to_string(GetLastError());
#else
    return std::to_string(errno);
#endif
}

std::string Path::join(const std::string& base, const std::string& name) {
#if defined(_WIN32) || defined(_WIN64)
    if (base.empty() || base.back() == '\\' || base.back() == '/') {
        return base + name;
    }
    return base + "\\" + name;
#else
    if (base.empty() || base.back() == '/') {
        return base + name;
    }
    return base + "/" + name;
#endif
}

std::string Path::join(const std::string& p1, const std::string& p2,
                       const std::string& p3) {
    return join(join(p1, p2), p3);
}

std::string Path::getDirectory(const std::string& path) {
    size_t pos = path.find_last_of("\\/");
    if (pos == std::string::npos) {
        return ".";
    }
    return path.substr(0, pos);
}

std::string Path::getFileName(const std::string& path) {
    size_t pos = path.find_last_of("\\/");
    if (pos == std::string::npos) {
        return path;
    }
    return path.substr(pos + 1);
}

std::string Path::getBasename(const std::string& path) {
    std::string name = getFileName(path);
    size_t pos = name.find_last_of('.');
    if (pos == std::string::npos || pos == 0) {
        return name;
    }
    return name.substr(0, pos);
}

std::string Path::getExtension(const std::string& path) {
    std::string name = getFileName(path);
    size_t pos = name.find_last_of('.');
    if (pos == std::string::npos) {
        return "";
    }
    return name.substr(pos);
}

}
