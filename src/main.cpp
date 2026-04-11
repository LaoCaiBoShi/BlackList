#define _CRT_SECURE_NO_WARNINGS
#include "blacklist_service.h"
#include "tcp_server.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <string>
#include <algorithm>
#include <limits>
#include <thread>

// 平台特定头文件
#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
// Linux平台不需要额外头文件
#endif

/**
 * @brief 查询卡号的交互式流程
 * @param service BlacklistService实例
 */
void queryCardLoop(BlacklistService& service) {
    std::cout << "\n==========================================" << std::endl;
    std::cout << "Blacklist Query Mode (Console)" << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "Status: " << service.getStatusString() << std::endl;
    std::cout << "Loaded: " << service.getBlacklistSize() << " records" << std::endl;
    std::cout << "\nCommands:" << std::endl;
    std::cout << "  - Enter card ID to check (20 digits)" << std::endl;
    std::cout << "  - 'status' : Show service status and version" << std::endl;
    std::cout << "  - 'update'  : Trigger background update with new ZIP file" << std::endl;
    std::cout << "  - 'help'    : Show this help message" << std::endl;
    std::cout << "  - 'quit'    : Exit program" << std::endl;

    std::string input;
    while (true) {
        std::cout << "\n> ";
        std::getline(std::cin, input);

        // 去除首尾空格
        input.erase(0, input.find_first_not_of(" \t\r\n"));
        input.erase(input.find_last_not_of(" \t\r\n") + 1);

        if (input.empty()) {
            continue;
        }

        // 退出命令
        if (input == "quit" || input == "exit" || input == "q") {
            std::cout << "Exiting..." << std::endl;
            break;
        }

        // 帮助命令
        if (input == "help" || input == "h" || input == "?") {
            std::cout << "\nCommands:" << std::endl;
            std::cout << "  - Enter card ID to check (20 digits)" << std::endl;
            std::cout << "  - 'status' : Show service status and version" << std::endl;
            std::cout << "  - 'update'  : Trigger background update with new ZIP file" << std::endl;
            std::cout << "  - 'help'    : Show this help message" << std::endl;
            std::cout << "  - 'quit'    : Exit program" << std::endl;
            continue;
        }

        // 状态命令
        if (input == "status" || input == "stat" || input == "s") {
            std::cout << "Status: " << service.getStatusString() << std::endl;
            std::cout << "Records: " << service.getBlacklistSize() << std::endl;
            std::cout << "Version: " << service.getVersionInfo() << std::endl;
            continue;
        }

        // 更新命令
        if (input == "update" || input == "u") {
            std::cout << "Enter new ZIP file path: ";
            std::string newPath;
            std::getline(std::cin, newPath);
            newPath.erase(0, newPath.find_first_not_of(" \t\r\n"));
            newPath.erase(newPath.find_last_not_of(" \t\r\n") + 1);

            if (newPath.empty()) {
                std::cout << "Update cancelled: empty path" << std::endl;
                continue;
            }

            std::cout << "Triggering background update with: " << newPath << std::endl;
            service.triggerUpdate(newPath);
            std::cout << "Background update started." << std::endl;
            std::cout << "Use 'status' to monitor progress." << std::endl;
            continue;
        }

        // 检查卡号是否在黑名单中
        if (input.length() == 20) {
            bool isBlacklisted = service.isBlacklisted(input);
            std::cout << "Result: " << (isBlacklisted ? "BLACKLISTED" : "NOT BLACKLISTED") << std::endl;
        } else {
            std::cout << "Invalid card ID: must be 20 digits" << std::endl;
            std::cout << "Use 'help' for available commands" << std::endl;
        }
    }
}

/**
 * @brief 主函数，程序的入口点
 * @param argc 命令行参数数量
 * @param argv 命令行参数数组
 * @return 程序执行状态
 */
int main(int argc, char* argv[]) {
    BlacklistService service;
    int tcpPort = 8888;  // 默认TCP端口
    std::string persistPath = "blacklist.dat";  // 持久化文件路径
    TcpQueryServer* tcpServer = nullptr;  // TCP查询服务器指针
    std::string zipPath;  // ZIP文件路径（可能为空）

    // 检查命令行参数
    bool hasZipPath = false;
    if (argc > 1) {
        zipPath = argv[1];
        hasZipPath = true;
        if (argc > 2) {
            tcpPort = atoi(argv[2]);
        }
        if (argc > 3) {
            persistPath = argv[3];
        }
    }

    std::cout << "==========================================" << std::endl;
    std::cout << "Blacklist Service - Startup" << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "Persist file: " << persistPath << std::endl;

    // 如果没有提供 ZIP 路径，优先检查持久化文件
    if (!hasZipPath) {
        if (service.isPersistFileValid(persistPath)) {
            std::cout << "Found valid persist file, will restore from it." << std::endl;
            std::cout << "(No ZIP file input required)" << std::endl;
        } else {
            // 需要输入 ZIP 路径来创建持久化文件
            std::cout << "No valid persist file found." << std::endl;
            std::cout << "Enter compressed file path: ";
            std::getline(std::cin, zipPath);
            hasZipPath = !zipPath.empty();
        }
    } else {
        std::cout << "ZIP file: " << zipPath << std::endl;
    }

    // 使用BlacklistService初始化
    // - 如果有持久化文件，会快速恢复并后台加载最新 ZIP
    // - 如果没有持久化但有 ZIP，会从 ZIP 加载并创建持久化文件
    // - 如果都没有，初始化会失败
    bool initSuccess = service.initialize(zipPath, persistPath);

    std::cout << "\n==========================================" << std::endl;
    std::cout << "Initialization Result" << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "Status: " << service.getStatusString() << std::endl;
    std::cout << "Loaded count: " << service.getBlacklistSize() << std::endl;

    // 如果初始化成功，启动TCP服务器并进入查询模式
    if (initSuccess && service.getBlacklistSize() > 0) {
        // 创建TCP查询服务器
        tcpServer = createTcpQueryServer(tcpPort, [&service](const std::string& cardId) -> std::string {
            bool isBlacklisted = service.isBlacklisted(cardId);
            return isBlacklisted ? "BLACKLISTED\n" : "NOT_BLACKLISTED\n";
        });
        tcpServer->start();

        std::cout << "\nTCP Query Server started on port " << tcpPort << std::endl;
        std::cout << "You can connect via: telnet localhost " << tcpPort << std::endl;

        // 进入交互式查询模式
        queryCardLoop(service);

        // 停止TCP服务器
        if (tcpServer) {
            tcpServer->stop();
            destroyTcpQueryServer(tcpServer);
        }
    } else {
        std::cout << "\nInitialization failed. Exiting..." << std::endl;
        std::cout << "Error: " << service.getLastError() << std::endl;
    }

    return 0;
}
