#include "blacklist_checker.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <string>
#include <algorithm>
#include <windows.h>
#include <limits>

// 加载压缩文件中的所有JSON文件
bool loadBlacklistFromCompressedFile(const std::string& compressedPath, BlacklistChecker& checker, size_t& totalLoaded, size_t& totalInvalid) {
    // 创建临时目录
    std::string tempDir = "temp_blacklist";
    
    // 构建解压命令
    std::string command = "powershell Expand-Archive -Path '" + compressedPath + "' -DestinationPath '" + tempDir + "' -Force";
    std::cout << "Extracting ZIP file..." << std::endl;
    
    // 执行解压命令
    int result = system(command.c_str());
    if (result != 0) {
        std::cout << "Error: Failed to extract ZIP file" << std::endl;
        return false;
    }
    
    // 遍历临时目录中的所有JSON文件并加载
    WIN32_FIND_DATA findData;
    HANDLE hFind = FindFirstFile((tempDir + "\\*.json").c_str(), &findData);
    
    if (hFind == INVALID_HANDLE_VALUE) {
        // 尝试递归搜索子目录
        std::cout << "No JSON files found in root, searching subdirectories..." << std::endl;
        
        // 遍历所有子目录
        std::string searchCmd = "powershell -Command \"Get-ChildItem -Path '" + tempDir + "' -Recurse -Filter *.json | Select-Object FullName\"";
        FILE* pipe = _popen(searchCmd.c_str(), "r");
        if (pipe) {
            char buffer[4096];
            while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
                std::string line = buffer;
                // 移除换行符
                line.erase(remove(line.begin(), line.end(), '\n'), line.end());
                line.erase(remove(line.begin(), line.end(), '\r'), line.end());
                
                // 跳过空行和标题行
                if (line.empty() || line.find("FullName") != std::string::npos || line.find("-----") != std::string::npos) {
                    continue;
                }
                
                if (!line.empty()) {
                    std::cout << "Loading: " << line << std::endl;
                    size_t localLoaded = 0;
                    size_t localInvalid = 0;
                    checker.loadFromJsonFile(line, localLoaded, localInvalid);
                    totalLoaded += localLoaded;
                    totalInvalid += localInvalid;
                }
            }
            _pclose(pipe);
        }
        
        // 清理临时目录
        std::string rmdirCmd = "powershell Remove-Item -Path '" + tempDir + "' -Recurse -Force";
        system(rmdirCmd.c_str());
        
        std::cout << "ZIP processing completed. Loaded: " << totalLoaded << ", Invalid: " << totalInvalid << std::endl;
        return totalLoaded > 0;
    }
    
    do {
        std::string filePath = tempDir + "\\" + findData.cFileName;
        std::cout << "Loading: " << filePath << std::endl;
        
        size_t localLoaded = 0;
        size_t localInvalid = 0;
        checker.loadFromJsonFile(filePath, localLoaded, localInvalid);
        totalLoaded += localLoaded;
        totalInvalid += localInvalid;
    } while (FindNextFile(hFind, &findData));
    
    FindClose(hFind);
    
    // 清理临时目录
    std::string rmdirCmd = "powershell Remove-Item -Path '" + tempDir + "' -Recurse -Force";
    system(rmdirCmd.c_str());
    
    std::cout << "ZIP processing completed. Loaded: " << totalLoaded << ", Invalid: " << totalInvalid << std::endl;
    return totalLoaded > 0;
}

int main() {
    BlacklistChecker checker;
    
    // 测试压缩文件加载
    std::string filePath = "D:/Coder/socket/BlackList/4401S0008440030010_DownLoad_20230620_CardBlacklistAll_20230620004813698_REQ_1_116.zip";
    
    // 显示测试信息
    std::cout << "==========================================" << std::endl;
    std::cout << "Blacklist Checker - Test Mode" << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "Testing compressed file loading..." << std::endl;
    std::cout << "File path: " << filePath << std::endl;
    
    // 记录加载开始时间
    auto startTime = std::chrono::high_resolution_clock::now();
    std::time_t start_time_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    
    // 加载文件
    size_t loadedCount = 0;
    size_t invalidCount = 0;
    bool loadResult = false;
    
    // 加载压缩文件中的JSON文件
    loadResult = loadBlacklistFromCompressedFile(filePath, checker, loadedCount, invalidCount);
    
    // 记录加载结束时间
    auto endTime = std::chrono::high_resolution_clock::now();
    std::time_t end_time_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    
    // 计算加载总时间
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    // 计算内存占用
    size_t memoryUsage = 0;
    
    // 输出加载结果
    std::cout << "\n==========================================" << std::endl;
    std::cout << "Loading Result" << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "Load result: " << (loadResult ? "Success" : "Failed") << std::endl;
    std::cout << "Loaded count: " << loadedCount << std::endl;
    std::cout << "Invalid count: " << invalidCount << std::endl;
    
    // 输出加载时间
    std::cout << "Load start time: " << std::put_time(std::localtime(&start_time_t), "%Y-%m-%d %H:%M:%S") << std::endl;
    std::cout << "Load end time: " << std::put_time(std::localtime(&end_time_t), "%Y-%m-%d %H:%M:%S") << std::endl;
    std::cout << "Load total time: " << duration.count() << " milliseconds" << std::endl;
    
    // 输出内存占用
    std::cout << "Memory usage: " << memoryUsage << " MB" << std::endl;
    
    // 输出加载的黑名单卡号总数量
    std::cout << "Total blacklist size: " << checker.size() << std::endl;
    std::cout << "==========================================" << std::endl;
    
    // 测试查询功能
    std::cout << "\n==========================================" << std::endl;
    std::cout << "Testing Query Function" << std::endl;
    std::cout << "==========================================" << std::endl;
    
    // 测试几个卡号
    std::vector<std::string> testCards = {
        "44012208232000831351",
        "44011934237059388712",
        "12345678901234567890"
    };
    
    for (const std::string& cardId : testCards) {
        bool isBlacklisted = checker.isBlacklisted(cardId);
        if (isBlacklisted) {
            std::cout << "Result: " << cardId << " is BLACKLISTED" << std::endl;
        } else {
            std::cout << "Result: " << cardId << " is NOT blacklisted" << std::endl;
        }
    }
    
    std::cout << "\n==========================================" << std::endl;
    std::cout << "Program terminated." << std::endl;
    std::cout << "==========================================" << std::endl;
    
    return 0;
}
