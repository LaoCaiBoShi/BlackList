#define _CRT_SECURE_NO_WARNINGS
#include "blacklist_service.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <string>
#include <algorithm>
#include <limits>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#endif

void queryCardLoop(BlacklistService& service) {
    std::cout << "\n==========================================" << std::endl;
    std::cout << "Blacklist Query Mode (Console)" << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "Status: " << service.getStatusString() << std::endl;
    std::cout << "Loaded: " << service.getBlacklistSize() << " records" << std::endl;
    std::cout << "\nCommands:" << std::endl;
    std::cout << "  - Enter card ID to check (20 digits)" << std::endl;
    std::cout << "  - 'status' : Show service status and version" << std::endl;
    std::cout << "  - 'quit'    : Exit program" << std::endl;

    std::string input;
    while (true) {
        std::cout << "\n> ";
        std::getline(std::cin, input);

        input.erase(0, input.find_first_not_of(" \t\r\n"));
        input.erase(input.find_last_not_of(" \t\r\n") + 1);

        if (input.empty()) {
            continue;
        }

        if (input == "quit" || input == "exit" || input == "q") {
            std::cout << "Exiting..." << std::endl;
            break;
        }

        if (input == "help" || input == "h" || input == "?") {
            std::cout << "\nCommands:" << std::endl;
            std::cout << "  - Enter card ID to check (20 digits)" << std::endl;
            std::cout << "  - 'status' : Show service status and version" << std::endl;
            std::cout << "  - 'quit'    : Exit program" << std::endl;
            continue;
        }

        if (input == "status" || input == "stat" || input == "s") {
            std::cout << "Status: " << service.getStatusString() << std::endl;
            std::cout << "Records: " << service.getBlacklistSize() << std::endl;
            std::cout << "Version: " << service.getVersionInfo() << std::endl;
            continue;
        }

        if (input.length() == 20) {
            bool isBlacklisted = service.isBlacklisted(input);
            std::cout << "Result: " << (isBlacklisted ? "BLACKLISTED" : "NOT BLACKLISTED") << std::endl;
        } else {
            std::cout << "Invalid card ID: must be 20 digits" << std::endl;
        }
    }
}

int main(int argc, char* argv[]) {
    std::cout << "==========================================" << std::endl;
    std::cout << "Blacklist Service - Startup" << std::endl;
    std::cout << "==========================================" << std::endl;

    std::string zipPath;
    if (argc > 1) {
        zipPath = argv[1];
        std::cout << "ZIP file: " << zipPath << std::endl;
    } else {
        std::cout << "Enter compressed file path: ";
        std::getline(std::cin, zipPath);
    }

    // 循环检查文件直到找到有效文件
    while (true) {
        if (zipPath.empty()) {
            std::cout << "\nError: No ZIP file specified" << std::endl;
        } else if (!std::filesystem::exists(zipPath)) {
            std::cout << "\n==========================================" << std::endl;
            std::cout << "Error: File not found" << std::endl;
            std::cout << "==========================================" << std::endl;
            std::cout << "The specified file does not exist: " << zipPath << std::endl;
            std::cout << "\nPlease check the file path and try again." << std::endl;
            std::cout << "\nUsage: " << std::endl;
            std::cout << "  1. Run the program: blacklist_checker.exe" << std::endl;
            std::cout << "  2. Enter the full path to the ZIP file" << std::endl;
            std::cout << "  3. Or drag and drop the file into the terminal" << std::endl;
            std::cout << "\nExample paths:" << std::endl;
            std::cout << "  D:\\Data\\blacklist.zip" << std::endl;
            std::cout << "  C:\\Users\\Admin\\Downloads\\data.zip" << std::endl;
            std::cout << "==========================================" << std::endl;
        } else {
            // 文件存在，跳出循环
            break;
        }

        // 提示重新输入
        std::cout << "\nPlease enter a valid ZIP file path (or 'quit' to exit): ";
        std::getline(std::cin, zipPath);

        // 清理输入首尾空白
        zipPath.erase(0, zipPath.find_first_not_of(" \t\r\n"));
        size_t endPos = zipPath.find_last_not_of(" \t\r\n");
        if (endPos != std::string::npos) {
            zipPath.erase(endPos + 1);
        } else {
            zipPath.clear();
        }

        // 只在明确输入quit命令时才退出，空输入继续等待
        if (!zipPath.empty() && (zipPath == "quit" || zipPath == "q" || zipPath == "exit")) {
            std::cout << "Exiting..." << std::endl;
            return 0;
        }
    }

    BlacklistService service;
    bool initSuccess = service.initialize(zipPath);

    std::cout << "\n==========================================" << std::endl;
    std::cout << "Initialization Result" << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "Status: " << service.getStatusString() << std::endl;
    std::cout << "Loaded count: " << service.getBlacklistSize() << std::endl;

    if (initSuccess && service.getBlacklistSize() > 0) {
        std::cout << "\nBlacklist loaded successfully!" << std::endl;
        queryCardLoop(service);
    } else {
        std::cout << "\nInitialization failed. Exiting..." << std::endl;
        std::cout << "Error: " << service.getLastError() << std::endl;
        return 1;
    }

    return 0;
}
