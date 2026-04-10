#ifndef ZIP_EXTRACTOR_H
#define ZIP_EXTRACTOR_H

#include <string>
#include <vector>
#include <functional>

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    #undef min
    #undef max
#endif

// Include zlib headers
#include "zlib/zlib.h"

class ZipExtractor
{
public:
    enum class ZipResult
    {
        OK,
        ERR_OPEN_ZIP,
        ERR_INVALID_ZIP,
        ERR_OPEN_FILE,
        ERR_READ_FILE,
        ERR_WRITE_FILE,
        ERR_CREATE_DIR,
        ERR_INVALID_PASSWORD,
        ERR_CRC,
        ERR_UNSUPPORTED,
        ERR_UNKNOWN
    };

    using ProgressCallback = std::function<void(const std::string&, int, int)>;

    ZipExtractor();
    ~ZipExtractor();

    ZipResult open(const std::string& zipPath);
    ZipResult extractAll(const std::string& destDir, ProgressCallback callback = nullptr);
    ZipResult extractAllWithCallback(const std::string& destDir, std::function<void(const std::string&)> fileCallback, ProgressCallback progressCallback = nullptr);
    ZipResult extractFile(const std::string& fileName, const std::string& destPath);
    void close();

    std::string getLastError() const;
    int getFileCount() const;
    std::vector<std::string> getFileList() const;

private:
    ZipResult createDirectoryRecursive(const std::string& path);
    std::string normalizePath(const std::string& path);
    std::string getParentPath(const std::string& path);
    std::string getErrorString(ZipResult result);

    std::string m_zipPath;
    std::string m_lastError;
    int m_fileCount;
    std::vector<std::string> m_fileList;
};

#endif