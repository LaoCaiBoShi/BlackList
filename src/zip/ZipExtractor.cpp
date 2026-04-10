#include "ZipExtractor.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <iomanip>

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    #include <direct.h>
    #include <shlwapi.h>
    #pragma comment(lib, "shlwapi.lib")
    #include <shellapi.h>
#else
    #include <sys/stat.h>
    #include <sys/types.h>
#endif

// Disable padding for structs to match ZIP file format
#pragma pack(push, 1)

// ZIP file header structure
struct ZipHeader {
    uint32_t signature;
    uint16_t version;
    uint16_t flags;
    uint16_t compression_method;
    uint16_t last_modified_time;
    uint16_t last_modified_date;
    uint32_t crc32_value;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint16_t file_name_length;
    uint16_t extra_field_length;
};

// Central directory header structure (46 bytes total)
struct CentralDirHeader {
    uint32_t signature;              // 4 bytes
    uint16_t version_made_by;        // 2 bytes
    uint16_t version_needed;         // 2 bytes
    uint16_t flags;                  // 2 bytes
    uint16_t compression_method;     // 2 bytes
    uint16_t last_modified_time;     // 2 bytes
    uint16_t last_modified_date;     // 2 bytes
    uint32_t crc32_value;            // 4 bytes
    uint32_t compressed_size;        // 4 bytes
    uint32_t uncompressed_size;      // 4 bytes
    uint16_t file_name_length;       // 2 bytes
    uint16_t extra_field_length;     // 2 bytes
    uint16_t file_comment_length;    // 2 bytes
    uint16_t disk_number_start;      // 2 bytes
    uint16_t internal_file_attributes; // 2 bytes
    uint32_t external_file_attributes; // 4 bytes
    uint32_t relative_offset_of_local_header; // 4 bytes
};

// Restore default packing
#pragma pack(pop)

/**
 * @brief ZipExtractor 类的构造函数
 * 初始化成员变量
 */
ZipExtractor::ZipExtractor()
    : m_fileCount(0)
{
}

/**
 * @brief ZipExtractor 类的析构函数
 * 调用 close() 方法清理资源
 */
ZipExtractor::~ZipExtractor()
{
    close();
}

/**
 * @brief 打开 ZIP 文件并读取其目录结构
 * @param zipPath ZIP 文件路径
 * @return 操作结果，ZipResult::OK 表示成功，其他值表示失败
 */
ZipExtractor::ZipResult ZipExtractor::open(const std::string& zipPath)
{
    close();

    m_zipPath = zipPath;
    
    // Open ZIP file
    std::ifstream file(zipPath, std::ios::binary);
    if (!file.is_open())
    {
        m_lastError = "Failed to open ZIP file: " + zipPath;
        return ZipResult::ERR_OPEN_ZIP;
    }

    // Find central directory by searching for EOCD signature
    file.seekg(0, std::ios::end);
    long long fileSize = file.tellg();
    long long searchStart = std::max(0LL, fileSize - 65536); // Search last 64KB
    file.seekg(searchStart, std::ios::beg);
    
    char buffer[65536];
    file.read(buffer, fileSize - searchStart);
    
    // Search for EOCD signature
    bool found = false;
    long long eocdPos = -1;
    for (long long i = (fileSize - searchStart) - 22; i >= 0; --i)
    {
        if (*(uint32_t*)(buffer + i) == 0x06054b50)
        {
            found = true;
            eocdPos = searchStart + i;
            break;
        }
    }
    
    if (!found)
    {
        m_lastError = "Invalid ZIP file: missing end of central directory record";
        file.close();
        return ZipResult::ERR_INVALID_ZIP;
    }
    
    // Read EOCD
    file.seekg(eocdPos, std::ios::beg);
    char eocd[22];
    file.read(eocd, 22);

    // Read central directory information
    uint16_t total_entries = *(uint16_t*)(eocd + 10);
    uint32_t central_dir_offset = *(uint32_t*)(eocd + 16);

    // Seek to central directory
    file.seekg(central_dir_offset, std::ios::beg);

    // Read each file entry in central directory
    for (int i = 0; i < total_entries; ++i)
    {
        CentralDirHeader cdh;
        file.read((char*)&cdh, sizeof(cdh));
        
        if (cdh.signature != 0x02014b50)
        {
            m_lastError = "Invalid ZIP file: bad central directory header";
            file.close();
            return ZipResult::ERR_INVALID_ZIP;
        }

        // Read file name
        char* fileName = new char[cdh.file_name_length + 1];
        file.read(fileName, cdh.file_name_length);
        fileName[cdh.file_name_length] = '\0';

        m_fileList.push_back(fileName);
        delete[] fileName;

        // Skip extra field and file comment
        file.seekg(cdh.extra_field_length + cdh.file_comment_length, std::ios::cur);
    }

    m_fileCount = total_entries;
    file.close();

    return ZipResult::OK;
}

/**
 * @brief 解压 ZIP 文件中的所有文件到指定目录
 * @param destDir 目标目录路径
 * @param callback 进度回调函数，可选
 * @return 操作结果，ZipResult::OK 表示成功，其他值表示失败
 */
ZipExtractor::ZipResult ZipExtractor::extractAll(const std::string& destDir, ProgressCallback callback)
{
    // 调用 extractAllWithCallback 但不提供文件回调
    return extractAllWithCallback(destDir, nullptr, callback);
}

ZipExtractor::ZipResult ZipExtractor::extractAllWithCallback(const std::string& destDir, std::function<void(const std::string&)> fileCallback, ProgressCallback progressCallback)
{
    ZipResult result = createDirectoryRecursive(destDir);
    if (result != ZipResult::OK)
    {
        return result;
    }

#if defined(_WIN32) || defined(_WIN64)
    // 使用 Windows API 来解压缩文件
    std::string command = "powershell -Command \"Expand-Archive -Path '" + m_zipPath + "' -DestinationPath '" + destDir + "' -Force\"";
    int exitCode = system(command.c_str());
    if (exitCode != 0)
    {
        m_lastError = "Failed to extract ZIP file using PowerShell";
        return ZipResult::ERR_READ_FILE;
    }

    // 模拟进度回调并调用文件回调
    int processed = 0;
    for (const std::string& fileName : m_fileList)
    {
        processed++;
        if (progressCallback)
        {
            progressCallback(fileName, processed, m_fileCount);
        }
        if (fileCallback)
        {
            std::string fullPath = destDir + "\\" + fileName;
            fileCallback(fullPath);
        }
    }

    return ZipResult::OK;
#else
    // 对于非 Windows 平台，使用原始的解压方法
    // Open ZIP file
    std::ifstream file(m_zipPath, std::ios::binary);
    if (!file.is_open())
    {
        m_lastError = "Failed to open ZIP file: " + m_zipPath;
        return ZipResult::ERR_OPEN_ZIP;
    }

    // Find central directory by searching for EOCD signature
    file.seekg(0, std::ios::end);
    long long fileSize = file.tellg();
    long long searchStart = std::max(0LL, fileSize - 65536); // Search last 64KB
    file.seekg(searchStart, std::ios::beg);
    
    char buffer[65536];
    file.read(buffer, fileSize - searchStart);
    
    // Search for EOCD signature
    bool found = false;
    long long eocdPos = -1;
    for (long long i = (fileSize - searchStart) - 22; i >= 0; --i)
    {
        if (*(uint32_t*)(buffer + i) == 0x06054b50)
        {
            found = true;
            eocdPos = searchStart + i;
            break;
        }
    }
    
    if (!found)
    {
        m_lastError = "Invalid ZIP file: missing end of central directory record";
        file.close();
        return ZipResult::ERR_INVALID_ZIP;
    }
    
    // Read EOCD
    file.seekg(eocdPos, std::ios::beg);
    char eocd[22];
    file.read(eocd, 22);
    uint32_t central_dir_offset = *(uint32_t*)(eocd + 16);

    // Seek to central directory
    file.seekg(central_dir_offset, std::ios::beg);

    int processed = 0;
    for (const std::string& fileName : m_fileList)
    {
        // Read central directory entry
        CentralDirHeader cdh;
        file.read((char*)&cdh, sizeof(cdh));

        // Skip file name, extra field and file comment
        file.seekg(cdh.file_name_length + cdh.extra_field_length + cdh.file_comment_length, std::ios::cur);

        // Seek to local file header
        file.seekg(cdh.relative_offset_of_local_header, std::ios::beg);

        // Read local file header
        ZipHeader zh;
        file.read((char*)&zh, sizeof(zh));

        // Skip file name and extra field
        file.seekg(zh.file_name_length + zh.extra_field_length, std::ios::cur);

        // Create destination file
        std::string fullPath = destDir + "/" + fileName;
        std::string parentDir = getParentPath(fullPath);
        if (!parentDir.empty())
        {
            result = createDirectoryRecursive(parentDir);
            if (result != ZipResult::OK)
            {
                file.close();
                return result;
            }
        }

        std::ofstream outFile(fullPath, std::ios::binary);
        if (!outFile.is_open())
        {
            m_lastError = "Failed to create file: " + fullPath;
            file.close();
            return ZipResult::ERR_WRITE_FILE;
        }

        // Extract file content (only uncompressed files for now)
        if (zh.compression_method == 0) // No compression
        {
            char buffer[8192];
            size_t remaining = zh.compressed_size;
            while (remaining > 0)
            {
                size_t readSize = std::min((size_t)8192, remaining);
                file.read(buffer, readSize);
                outFile.write(buffer, readSize);
                remaining -= readSize;
            }
        }
        else
        {
            m_lastError = "Unsupported compression method: " + std::to_string(zh.compression_method);
            outFile.close();
            file.close();
            return ZipResult::ERR_UNSUPPORTED;
        }

        outFile.close();
        processed++;

        if (progressCallback)
        {
            progressCallback(fileName, processed, m_fileCount);
        }
        if (fileCallback)
        {
            fileCallback(fullPath);
        }

        // Seek back to central directory for next entry
        central_dir_offset += sizeof(CentralDirHeader) + cdh.file_name_length + cdh.extra_field_length + cdh.file_comment_length;
        file.seekg(central_dir_offset, std::ios::beg);
    }

    file.close();
    return ZipResult::OK;
#endif
}

/**
 * @brief 从 ZIP 文件中提取指定文件到目标路径
 * @param fileName ZIP 中的文件名
 * @param destPath 目标文件路径
 * @return 操作结果，ZipResult::OK 表示成功，其他值表示失败
 */
ZipExtractor::ZipResult ZipExtractor::extractFile(const std::string& fileName, const std::string& destPath)
{
    std::string parentDir = getParentPath(destPath);
    if (!parentDir.empty())
    {
        ZipResult result = createDirectoryRecursive(parentDir);
        if (result != ZipResult::OK)
        {
            return result;
        }
    }

#if defined(_WIN32) || defined(_WIN64)
    // 使用 Windows API 来解压缩文件
    std::string tempDir = destPath + "_temp";
    createDirectoryRecursive(tempDir);
    
    std::string command = "powershell -Command \"Expand-Archive -Path '" + m_zipPath + "' -DestinationPath '" + tempDir + "' -Force\"";
    int exitCode = system(command.c_str());
    if (exitCode != 0)
    {
        m_lastError = "Failed to extract ZIP file using PowerShell";
        return ZipResult::ERR_READ_FILE;
    }

    // 复制指定文件到目标路径
    std::string sourcePath = tempDir + "\\" + fileName;
    if (CopyFileA(sourcePath.c_str(), destPath.c_str(), FALSE) == 0)
    {
        m_lastError = "Failed to copy file: " + sourcePath;
        return ZipResult::ERR_WRITE_FILE;
    }

    // 删除临时目录
    RemoveDirectoryA(tempDir.c_str());

    return ZipResult::OK;
#else
    // 对于非 Windows 平台，使用原始的解压方法
    // Open ZIP file
    std::ifstream file(m_zipPath, std::ios::binary);
    if (!file.is_open())
    {
        m_lastError = "Failed to open ZIP file: " + m_zipPath;
        return ZipResult::ERR_OPEN_ZIP;
    }

    // Find central directory by searching for EOCD signature
    file.seekg(0, std::ios::end);
    long long fileSize = file.tellg();
    long long searchStart = std::max(0LL, fileSize - 65536); // Search last 64KB
    file.seekg(searchStart, std::ios::beg);
    
    char buffer[65536];
    file.read(buffer, fileSize - searchStart);
    
    // Search for EOCD signature
    bool found = false;
    long long eocdPos = -1;
    for (long long i = (fileSize - searchStart) - 22; i >= 0; --i)
    {
        if (*(uint32_t*)(buffer + i) == 0x06054b50)
        {
            found = true;
            eocdPos = searchStart + i;
            break;
        }
    }
    
    if (!found)
    {
        m_lastError = "Invalid ZIP file: missing end of central directory record";
        file.close();
        return ZipResult::ERR_INVALID_ZIP;
    }
    
    // Read EOCD
    file.seekg(eocdPos, std::ios::beg);
    char eocd[22];
    file.read(eocd, 22);
    uint32_t central_dir_offset = *(uint32_t*)(eocd + 16);
    uint16_t total_entries = *(uint16_t*)(eocd + 10);

    // Seek to central directory
    file.seekg(central_dir_offset, std::ios::beg);

    bool fileFound = false;
    uint32_t local_header_offset = 0;
    ZipHeader zh;

    // Find the specified file
    for (int i = 0; i < total_entries; ++i)
    {
        CentralDirHeader cdh;
        file.read((char*)&cdh, sizeof(cdh));

        // Read file name
        char* currentFileName = new char[cdh.file_name_length + 1];
        file.read(currentFileName, cdh.file_name_length);
        currentFileName[cdh.file_name_length] = '\0';

        if (std::string(currentFileName) == fileName)
        {
            fileFound = true;
            local_header_offset = cdh.relative_offset_of_local_header;
            delete[] currentFileName;
            break;
        }

        delete[] currentFileName;
        // Skip extra field and file comment
        file.seekg(cdh.extra_field_length + cdh.file_comment_length, std::ios::cur);
    }

    if (!fileFound)
    {
        m_lastError = "File not found: " + fileName;
        file.close();
        return ZipResult::ERR_OPEN_FILE;
    }

    // Seek to local file header
    file.seekg(local_header_offset, std::ios::beg);

    // Read local file header
    file.read((char*)&zh, sizeof(zh));

    // Skip file name and extra field
    file.seekg(zh.file_name_length + zh.extra_field_length, std::ios::cur);

    // Create destination file
    std::ofstream outFile(destPath, std::ios::binary);
    if (!outFile.is_open())
    {
        m_lastError = "Failed to create file: " + destPath;
        file.close();
        return ZipResult::ERR_WRITE_FILE;
    }

    // Extract file content (only uncompressed files for now)
    if (zh.compression_method == 0) // No compression
    {
        char buffer[8192];
        size_t remaining = zh.compressed_size;
        while (remaining > 0)
        {
            size_t readSize = std::min((size_t)8192, remaining);
            file.read(buffer, readSize);
            outFile.write(buffer, readSize);
            remaining -= readSize;
        }
    }
    else
    {
        m_lastError = "Unsupported compression method: " + std::to_string(zh.compression_method);
        outFile.close();
        file.close();
        return ZipResult::ERR_UNSUPPORTED;
    }

    outFile.close();
    file.close();

    return ZipResult::OK;
#endif
}

/**
 * @brief 关闭 ZIP 文件并清理资源
 */
void ZipExtractor::close()
{
    m_fileList.clear();
    m_fileCount = 0;
}

/**
 * @brief 获取最后一次操作的错误信息
 * @return 错误信息字符串
 */
std::string ZipExtractor::getLastError() const
{
    return m_lastError;
}

/**
 * @brief 获取 ZIP 文件中的文件数量
 * @return 文件数量
 */
int ZipExtractor::getFileCount() const
{
    return m_fileCount;
}

/**
 * @brief 获取 ZIP 文件中的文件列表
 * @return 文件路径列表
 */
std::vector<std::string> ZipExtractor::getFileList() const
{
    return m_fileList;
}

/**
 * @brief 递归创建目录结构
 * @param path 要创建的目录路径
 * @return 操作结果，ZipResult::OK 表示成功，其他值表示失败
 */
ZipExtractor::ZipResult ZipExtractor::createDirectoryRecursive(const std::string& path)
{
    try
    {
        std::string currentPath = path;
        size_t pos = 0;

        do
        {
            pos = currentPath.find_first_of("/\\", pos + 1);
            std::string dir = currentPath.substr(0, pos);

            if (!dir.empty())
            {
#if defined(_WIN32) || defined(_WIN64)
                if (_mkdir(dir.c_str()) != 0 && errno != EEXIST)
#else
                if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST)
#endif
                {
                    m_lastError = "Failed to create directory: " + dir;
                    return ZipResult::ERR_CREATE_DIR;
                }
            }
        } while (pos != std::string::npos);
    }
    catch (const std::exception& e)
    {
        m_lastError = "Exception creating directory: " + std::string(e.what());
        return ZipResult::ERR_CREATE_DIR;
    }

    return ZipResult::OK;
}

/**
 * @brief 规范化路径，将反斜杠转换为正斜杠，并去除重复的斜杠
 * @param path 原始路径
 * @return 规范化后的路径
 */
std::string ZipExtractor::normalizePath(const std::string& path)
{
    std::string result = path;

    for (char& c : result)
    {
        if (c == '\\')
        {
            c = '/';
        }
    }

    while (result.find("//") != std::string::npos)
    {
        result.replace(result.find("//"), 2, "/");
    }

    return result;
}

/**
 * @brief 获取文件路径的父目录
 * @param path 文件路径
 * @return 父目录路径，若没有父目录则返回空字符串
 */
std::string ZipExtractor::getParentPath(const std::string& path)
{
    size_t lastSep = path.find_last_of("/\\");
    if (lastSep != std::string::npos)
    {
        return path.substr(0, lastSep);
    }
    return "";
}

/**
 * @brief 将 ZipResult 枚举值转换为错误信息字符串
 * @param result ZipResult 枚举值
 * @return 错误信息字符串
 */
std::string ZipExtractor::getErrorString(ZipResult result)
{
    switch (result)
    {
    case ZipResult::OK: return "Operation successful";
    case ZipResult::ERR_OPEN_ZIP: return "Failed to open ZIP file";
    case ZipResult::ERR_INVALID_ZIP: return "Invalid ZIP file";
    case ZipResult::ERR_OPEN_FILE: return "Failed to open source file";
    case ZipResult::ERR_READ_FILE: return "Failed to read file";
    case ZipResult::ERR_WRITE_FILE: return "Failed to write file";
    case ZipResult::ERR_CREATE_DIR: return "Failed to create directory";
    case ZipResult::ERR_INVALID_PASSWORD: return "Invalid password";
    case ZipResult::ERR_CRC: return "CRC check failed";
    case ZipResult::ERR_UNSUPPORTED: return "Unsupported format";
    default: return "Unknown error";
    }
}
