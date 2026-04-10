#include "zip_basic.h"
#include "ZipExtractor.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <string>
#include <vector>

/**
 * @brief 解压ZIP文件到指定目录
 * @param zipPath ZIP文件路径
 * @param destDir 目标目录
 * @return 解压是否成功
 */
bool ZipBasic::extractZip(const std::string& zipPath, const std::string& destDir) {
    std::cout << "Extracting: " << zipPath << " to " << destDir << std::endl;
    
    ZipExtractor extractor;
    ZipExtractor::ZipResult result = extractor.open(zipPath);
    if (result != ZipExtractor::ZipResult::OK) {
        std::cout << "Error: Failed to open ZIP file: " << extractor.getLastError() << std::endl;
        return false;
    }
    
    // 定义进度回调函数
    auto progressCallback = [](const std::string& fileName, int processed, int total) {
        std::cout << "Extracting: " << fileName << " (" << processed << "/" << total << ")" << std::endl;
    };
    
    result = extractor.extractAll(destDir, progressCallback);
    if (result != ZipExtractor::ZipResult::OK) {
        std::cout << "Error: Failed to extract ZIP file: " << extractor.getLastError() << std::endl;
        extractor.close();
        return false;
    }
    
    extractor.close();
    return true;
}

/**
 * @brief 获取ZIP文件中的文件列表
 * @param zipPath ZIP文件路径
 * @param fileList 输出参数，存储文件列表
 * @return 获取是否成功
 */
bool ZipBasic::getZipFileList(const std::string& zipPath, std::vector<std::string>& fileList) {
    ZipExtractor extractor;
    ZipExtractor::ZipResult result = extractor.open(zipPath);
    if (result != ZipExtractor::ZipResult::OK) {
        std::cout << "Error: Failed to open ZIP file: " << extractor.getLastError() << std::endl;
        return false;
    }
    
    fileList = extractor.getFileList();
    extractor.close();
    return true;
}

/**
 * @brief 从ZIP文件中提取指定文件
 * @param zipPath ZIP文件路径
 * @param fileName ZIP中的文件名
 * @param destPath 目标文件路径
 * @return 提取是否成功
 */
bool ZipBasic::extractFileFromZip(const std::string& zipPath, const std::string& fileName, const std::string& destPath) {
    ZipExtractor extractor;
    ZipExtractor::ZipResult result = extractor.open(zipPath);
    if (result != ZipExtractor::ZipResult::OK) {
        std::cout << "Error: Failed to open ZIP file: " << extractor.getLastError() << std::endl;
        return false;
    }
    
    result = extractor.extractFile(fileName, destPath);
    if (result != ZipExtractor::ZipResult::OK) {
        std::cout << "Error: Failed to extract file: " << extractor.getLastError() << std::endl;
        extractor.close();
        return false;
    }
    
    extractor.close();
    return true;
}
