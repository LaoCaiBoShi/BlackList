#define _CRT_SECURE_NO_WARNINGS
#include "blacklist_checker.h"
#include "zip_utils.h"
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
 * @param checker BlacklistChecker实例
 */
void queryCardLoop(BlacklistChecker& checker) {
    std::cout << "\n==========================================" << std::endl;
    std::cout << "Blacklist Query Mode (Console)" << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "Enter card ID to check (or 'quit' to exit):" << std::endl;
    
    std::string cardId;
    while (true) {
        std::cout << "\nCard ID: ";
        std::getline(std::cin, cardId);
        
        // 去除首尾空格
        cardId.erase(0, cardId.find_first_not_of(" \t\r\n"));
        cardId.erase(cardId.find_last_not_of(" \t\r\n") + 1);
        
        if (cardId.empty()) {
            continue;
        }
        
        if (cardId == "quit" || cardId == "exit" || cardId == "q") {
            std::cout << "Exiting..." << std::endl;
            break;
        }
        
        // 检查卡号是否在黑名单中
        bool isBlacklisted = checker.isBlacklisted(cardId);
        
        if (isBlacklisted) {
            std::cout << "Result: BLACKLISTED" << std::endl;
        } else {
            std::cout << "Result: NOT BLACKLISTED" << std::endl;
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
    BlacklistChecker checker;
    int tcpPort = 8888;  // 默认TCP端口
    TcpQueryServer* tcpServer = nullptr;  // TCP查询服务器指针

    // 检查命令行参数
    std::string path;
    if (argc > 1) {
        path = argv[1];
        // 如果提供了端口号，使用提供的端口号
        if (argc > 2) {
            tcpPort = atoi(argv[2]);
        }
    } else {
        // 直接实现交互式测试
        std::cout << "Enter compressed file path: " << std::endl;
        std::cout << "File path: ";
        std::getline(std::cin, path);
    }

    std::cout << "==========================================" << std::endl;
    std::cout << "Blacklist Checker - Batch Extract Test" << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "Extracting ZIP file: " << path << std::endl;

    size_t loaded = 0;
    size_t invalid = 0;
    bool success = loadBlacklistFromCompressedFile(path, checker, loaded, invalid);

    std::cout << "\n==========================================" << std::endl;
    std::cout << "Extraction Result" << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "Extract result: " << (success ? "Success" : "Failed") << std::endl;
    std::cout << "Loaded count: " << loaded << std::endl;
    std::cout << "Invalid count: " << invalid << std::endl;
    std::cout << "Total blacklist size: " << checker.size() << std::endl;
    
    // 输出加载统计信息
    checker.printLoadingStats();

    // 如果加载成功，启动TCP服务器并进入查询模式
    if (success && checker.size() > 0) {
        // 创建TCP查询服务器
        tcpServer = createTcpQueryServer(tcpPort, [&checker](const std::string& cardId) -> std::string {
            bool isBlacklisted = checker.isBlacklisted(cardId);
            return isBlacklisted ? "BLACKLISTED\n" : "NOT_BLACKLISTED\n";
        });
        tcpServer->start();

        std::cout << "\nTCP Query Server started on port " << tcpPort << std::endl;
        std::cout << "You can connect via: telnet localhost " << tcpPort << std::endl;

        // 进入交互式查询模式
        queryCardLoop(checker);

        // 停止TCP服务器
        if (tcpServer) {
            tcpServer->stop();
            destroyTcpQueryServer(tcpServer);
        }
    }

    return 0;
}
