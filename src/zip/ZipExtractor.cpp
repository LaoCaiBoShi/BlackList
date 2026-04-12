#include "ZipExtractor.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <memory>

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    #include <direct.h>
#else
    #include <sys/stat.h>
    #include <sys/types.h>
#endif

// minizip 头文件
#include "minizip/unzip.h"
#include "minizip/ioapi.h"

/**
 * @brief ZipExtractor 类的构造函数
 */
ZipExtractor::ZipExtractor()
    : m_zipFile(nullptr)
    , m_fileCount(0)
{
}

/**
 * @brief ZipExtractor 类的析构函数
 */
ZipExtractor::~ZipExtractor()
{
    close();
}

/**
 * @brief 打开 ZIP 文件并读取其目录结构
 */
ZipExtractor::ZipResult ZipExtractor::open(const std::string& zipPath)
{
    close();

    m_zipPath = zipPath;

#if defined(_WIN32) || defined(_WIN64)
    m_zipFile = unzOpen2(zipPath.c_str(), NULL);
#else
    m_zipFile = unzOpen2(zipPath.c_str(), NULL);
#endif

    if (m_zipFile == nullptr)
    {
        m_lastError = "Failed to open ZIP file: " + zipPath;
        return ZipResult::ERR_OPEN_ZIP;
    }

    // 获取文件列表
    m_fileList.clear();
    m_fileCount = 0;

    if (unzGoToFirstFile(m_zipFile) == UNZ_OK)
    {
        do {
            char filename[512];
            unz_file_info file_info;
            if (unzGetCurrentFileInfo(m_zipFile, &file_info, filename, sizeof(filename), NULL, 0, NULL, 0) == UNZ_OK)
            {
                m_fileList.push_back(filename);
                m_fileCount++;
            }
        } while (unzGoToNextFile(m_zipFile) == UNZ_OK);
    }

    return ZipResult::OK;
}

/**
 * @brief 关闭 ZIP 文件
 */
void ZipExtractor::close()
{
    if (m_zipFile != nullptr)
    {
        unzClose(m_zipFile);
        m_zipFile = nullptr;
    }
    m_fileList.clear();
    m_fileCount = 0;
}

/**
 * @brief 创建目录（支持递归创建）
 */
ZipExtractor::ZipResult ZipExtractor::createDirectoryRecursive(const std::string& path)
{
    if (path.empty()) return ZipResult::OK;

#if defined(_WIN32) || defined(_WIN64)
    std::string fullPath = path;
    for (size_t i = 0; i < fullPath.length(); ++i) {
        if (fullPath[i] == '/') {
            fullPath[i] = '\\';
        }
    }

    for (size_t i = 0; i < fullPath.length(); ++i) {
        if (fullPath[i] == '\\' && i > 0 && fullPath[i-1] != ':') {
            std::string dir = fullPath.substr(0, i);
            CreateDirectoryA(dir.c_str(), NULL);
        }
    }
    CreateDirectoryA(fullPath.c_str(), NULL);
#else
    std::string fullPath = path;
    for (size_t i = 0; i < fullPath.length(); ++i) {
        if (fullPath[i] == '\\') {
            fullPath[i] = '/';
        }
    }

    for (size_t i = 0; i < fullPath.length(); ++i) {
        if (fullPath[i] == '/' && i > 0) {
            mkdir(fullPath.substr(0, i).c_str(), 0755);
        }
    }
    mkdir(fullPath.c_str(), 0755);
#endif
    return ZipResult::OK;
}

/**
 * @brief 解压所有文件到目标目录
 */
ZipExtractor::ZipResult ZipExtractor::extractAll(const std::string& destDir, ProgressCallback callback)
{
    return extractAllWithCallback(destDir, nullptr, callback);
}

/**
 * @brief 解压所有文件到目标目录，并提供文件级回调
 */
ZipExtractor::ZipResult ZipExtractor::extractAllWithCallback(const std::string& destDir, std::function<void(const std::string&)> fileCallback, ProgressCallback progressCallback)
{
    if (m_zipFile == nullptr)
    {
        m_lastError = "ZIP file not opened";
        return ZipResult::ERR_OPEN_ZIP;
    }

    ZipResult result = createDirectoryRecursive(destDir);
    if (result != ZipResult::OK)
    {
        return result;
    }

    int processed = 0;
    if (unzGoToFirstFile(m_zipFile) == UNZ_OK)
    {
        do {
            char filename[512];
            unz_file_info file_info;
            if (unzGetCurrentFileInfo(m_zipFile, &file_info, filename, sizeof(filename), NULL, 0, NULL, 0) == UNZ_OK)
            {
                processed++;
                if (progressCallback)
                {
                    progressCallback(filename, processed, m_fileCount);
                }

                // 构建目标文件路径
                std::string destPath = destDir + "\\" + filename;

                // 如果是目录，创建目录
                size_t fnLen = strlen(filename);
                if (fnLen > 0 && (filename[fnLen - 1] == '/' || filename[fnLen - 1] == '\\'))
                {
                    createDirectoryRecursive(destPath);
                }
                else
                {
                    // 确保目录存在
                    size_t lastSlash = destPath.find_last_of("\\/");
                    if (lastSlash != std::string::npos)
                    {
                        createDirectoryRecursive(destPath.substr(0, lastSlash));
                    }

                    // 打开并解压文件
                    if (unzOpenCurrentFilePassword(m_zipFile, NULL) == UNZ_OK)
                    {
#if defined(_WIN32) || defined(_WIN64)
                        FILE* outFile = fopen(destPath.c_str(), "wb");
#else
                        FILE* outFile = fopen(destPath.c_str(), "wb");
#endif

                        if (outFile)
                        {
                            const int BUFFER_SIZE = 8192;
                            char buffer[BUFFER_SIZE];
                            int bytesRead = 0;
                            while ((bytesRead = unzReadCurrentFile(m_zipFile, buffer, BUFFER_SIZE)) > 0)
                            {
                                fwrite(buffer, 1, bytesRead, outFile);
                            }
                            fclose(outFile);

                            if (fileCallback)
                            {
                                fileCallback(filename);
                            }
                        }
                        unzCloseCurrentFile(m_zipFile);
                    }
                }
            }
        } while (unzGoToNextFile(m_zipFile) == UNZ_OK);
    }

    return ZipResult::OK;
}

/**
 * @brief 解压所有文件到目标目录，并提供实时文件回调
 */
ZipExtractor::ZipResult ZipExtractor::extractAllWithRealTimeCallback(const std::string& destDir, FileExtractedCallback fileCallback, ProgressCallback progressCallback)
{
    return extractAllWithCallback(destDir, fileCallback, progressCallback);
}

/**
 * @brief 解压单个文件
 */
ZipExtractor::ZipResult ZipExtractor::extractFile(const std::string& fileName, const std::string& destPath)
{
    if (m_zipFile == nullptr)
    {
        m_lastError = "ZIP file not opened";
        return ZipResult::ERR_OPEN_ZIP;
    }

    if (unzLocateFile(m_zipFile, fileName.c_str(), 0) != UNZ_OK)
    {
        m_lastError = "File not found in ZIP: " + fileName;
        return ZipResult::ERR_OPEN_FILE;
    }

    unz_file_info file_info;
    if (unzGetCurrentFileInfo(m_zipFile, &file_info, NULL, 0, NULL, 0, NULL, 0) != UNZ_OK)
    {
        m_lastError = "Failed to get file info: " + fileName;
        return ZipResult::ERR_READ_FILE;
    }

    // 确保目录存在
    size_t lastSlash = destPath.find_last_of("\\/");
    if (lastSlash != std::string::npos)
    {
        createDirectoryRecursive(destPath.substr(0, lastSlash));
    }

    if (unzOpenCurrentFilePassword(m_zipFile, NULL) != UNZ_OK)
    {
        m_lastError = "Failed to open file in ZIP: " + fileName;
        return ZipResult::ERR_READ_FILE;
    }

#if defined(_WIN32) || defined(_WIN64)
    FILE* outFile = fopen(destPath.c_str(), "wb");
#else
    FILE* outFile = fopen(destPath.c_str(), "wb");
#endif

    if (!outFile)
    {
        unzCloseCurrentFile(m_zipFile);
        m_lastError = "Failed to create output file: " + destPath;
        return ZipResult::ERR_WRITE_FILE;
    }

    const int BUFFER_SIZE = 8192;
    char buffer[BUFFER_SIZE];
    int bytesRead = 0;
    while ((bytesRead = unzReadCurrentFile(m_zipFile, buffer, BUFFER_SIZE)) > 0)
    {
        fwrite(buffer, 1, bytesRead, outFile);
    }

    fclose(outFile);
    unzCloseCurrentFile(m_zipFile);

    return ZipResult::OK;
}

/**
 * @brief 获取最近一次错误信息
 */
std::string ZipExtractor::getLastError() const
{
    return m_lastError;
}

/**
 * @brief 获取ZIP内的文件数量
 */
int ZipExtractor::getFileCount() const
{
    return m_fileCount;
}

/**
 * @brief 获取ZIP内的文件列表
 */
std::vector<std::string> ZipExtractor::getFileList() const
{
    return m_fileList;
}
