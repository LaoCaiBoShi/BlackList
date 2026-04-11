/**
 * @file test_main.cpp
 * @brief 黑名单系统测试程序入口
 */

#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <cassert>

#include "test_blacklist.h"

struct TestResult {
    std::string name;
    bool passed;
    std::string message;
    double elapsedMs;
};

std::vector<TestResult> g_testResults;

void printBanner(const std::string& title) {
    std::cout << "\n========================================" << std::endl;
    std::cout << "  " << title << std::endl;
    std::cout << "========================================" << std::endl;
}

void printResult(const TestResult& result) {
    std::string status = result.passed ? "[PASS]" : "[FAIL]";
    std::cout << status << " " << result.name;
    if (!result.message.empty()) {
        std::cout << ": " << result.message;
    }
    std::cout << " (" << result.elapsedMs << " ms)" << std::endl;
}

void printSummary() {
    printBanner("Test Summary");

    int passed = 0;
    int failed = 0;

    for (const auto& result : g_testResults) {
        printResult(result);
        if (result.passed) {
            passed++;
        } else {
            failed++;
        }
    }

    std::cout << "\n----------------------------------------" << std::endl;
    std::cout << "Total: " << g_testResults.size()
              << " | Passed: " << passed
              << " | Failed: " << failed << std::endl;
    std::cout << "========================================" << std::endl;

    if (failed > 0) {
        std::cout << "\n[ERROR] Some tests failed!" << std::endl;
        exit(1);
    } else {
        std::cout << "\n[SUCCESS] All tests passed!" << std::endl;
    }
}

void runTest(const std::string& name, std::function<bool()> testFunc) {
    TestResult result;
    result.name = name;

    auto start = std::chrono::high_resolution_clock::now();

    try {
        result.passed = testFunc();
        result.message = result.passed ? "" : "Test returned false";
    } catch (const std::exception& e) {
        result.passed = false;
        result.message = std::string("Exception: ") + e.what();
    } catch (...) {
        result.passed = false;
        result.message = "Unknown exception";
    }

    auto end = std::chrono::high_resolution_clock::now();
    result.elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();

    g_testResults.push_back(result);
}

int main(int argc, char* argv[]) {
    std::string zipPath = "D:\\Coder\\socket\\BlackList\\4401S0008440030010_DownLoad_20230620_CardBlacklistAll_20230620004813698_REQ_1_116.zip";
    std::string persistPath = "blacklist_test.dat";

    if (argc > 1) {
        zipPath = argv[1];
    }
    if (argc > 2) {
        persistPath = argv[2];
    }

    printBanner("Blacklist System Tests");
    std::cout << "ZIP file: " << zipPath << std::endl;
    std::cout << "Persist file: " << persistPath << std::endl;

    runTest("ZIP Loading", [&]() {
        return testLoadFromZip(zipPath);
    });

    runTest("Persist Save", [&]() {
        return testPersistSave(persistPath);
    });

    runTest("Persist Load", [&]() {
        return testPersistLoad(persistPath);
    });

    runTest("Query BLACKLISTED Card", [&]() {
        return testQueryBlacklisted();
    });

    runTest("Query NOT BLACKLISTED Card", [&]() {
        return testQueryNotBlacklisted();
    });

    runTest("Data Consistency", [&]() {
        return testDataConsistency();
    });

    printSummary();

    return 0;
}