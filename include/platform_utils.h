/**
 * @file platform_utils.h
 * @brief 跨平台文件系统工具类
 *
 * 提供统一的跨平台文件系统和路径操作接口
 */

#ifndef PLATFORM_UTILS_H
#define PLATFORM_UTILS_H

#include <string>
#include <vector>
#include <cstdint>
#include <ctime>

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    #include <sys/stat.h>
#else
    #include <cerrno>
    #include <cstring>
    #include <dirent.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

namespace platform {

struct FileInfo {
    std::string name;
    bool isDirectory;
    uint64_t size;
    time_t modifiedTime;
};

class FileSystem {
public:
    static FileSystem& instance();

    std::string getExecutablePath();

    bool createDirectory(const std::string& path);

    bool directoryExists(const std::string& path);

    std::vector<FileInfo> listFiles(const std::string& path,
                                    const std::string& pattern = "");

    bool fileExists(const std::string& path);

    uint64_t getFileSize(const std::string& path);

    time_t getFileModifiedTime(const std::string& path);

    std::string getLastErrorString();

    std::string getLastErrorCode();

private:
    FileSystem() = default;
    ~FileSystem() = default;
    FileSystem(const FileSystem&) = delete;
    FileSystem& operator=(const FileSystem&) = delete;

#if !defined(_WIN32) && !defined(_WIN64)
    std::string getExecutablePathLinux();
    std::vector<FileInfo> listFilesLinux(const std::string& path,
                                         const std::string& pattern);
#endif
};

class Path {
public:
    static std::string join(const std::string& base, const std::string& name);
    static std::string join(const std::string& p1, const std::string& p2,
                           const std::string& p3);
    static std::string getDirectory(const std::string& path);
    static std::string getFileName(const std::string& path);
    static std::string getBasename(const std::string& path);
    static std::string getExtension(const std::string& path);
};

}

#endif // PLATFORM_UTILS_H
