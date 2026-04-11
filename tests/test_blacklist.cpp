/**
 * @file test_blacklist.cpp
 * @brief 黑名单加载测试实现
 */

#include "test_blacklist.h"
#include "../include/blacklist_checker.h"
#include "../include/persist_reader.h"
#include "../include/zip_utils.h"

#include <iostream>
#include <fstream>
#include <cassert>

static BlacklistChecker* g_checker = nullptr;
static PersistReader* g_reader = nullptr;
static size_t g_loadedCount = 0;

bool testLoadFromZip(const std::string& zipPath) {
    std::cout << "\n[TEST] Loading from ZIP: " << zipPath << std::endl;

    g_checker = new BlacklistChecker();

    size_t loaded = 0;
    size_t invalid = 0;

    bool success = loadBlacklistFromCompressedFile(zipPath, *g_checker, loaded, invalid);

    if (success) {
        g_loadedCount = loaded;
        std::cout << "[TEST] Loaded: " << loaded << " records, Invalid: " << invalid << std::endl;
        std::cout << "[TEST] Checker size: " << g_checker->size() << std::endl;
    } else {
        std::cout << "[TEST] Failed to load from ZIP" << std::endl;
    }

    return success && (loaded > 0);
}

bool testPersistSave(const std::string& persistPath) {
    std::cout << "\n[TEST] Testing persist save: " << persistPath << std::endl;

    if (!g_checker) {
        std::cout << "[TEST] Checker not initialized" << std::endl;
        return false;
    }

    bool success = g_checker->savePersistAfterLoad(persistPath);

    if (success) {
        std::ifstream file(persistPath, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            auto size = file.tellg();
            std::cout << "[TEST] Persist file size: " << size << " bytes" << std::endl;
            file.close();
        }
    }

    return success;
}

bool testPersistLoad(const std::string& persistPath) {
    std::cout << "\n[TEST] Testing persist load: " << persistPath << std::endl;

    if (!g_reader) {
        g_reader = new PersistReader();
    }

    if (!PersistReader::isValid(persistPath)) {
        std::cout << "[TEST] Persist file is not valid" << std::endl;
        return false;
    }

    bool success = g_reader->open(persistPath);
    if (success) {
        std::cout << "[TEST] mmap opened: " << g_reader->getTotalCards()
                  << " records, " << g_reader->getPrefixCount() << " provinces" << std::endl;
    }

    return success;
}

bool testQueryBlacklisted() {
    std::cout << "\n[TEST] Testing BLACKLISTED card query" << std::endl;

    std::string testCard = "11011201230003140853";
    std::cout << "[TEST] Query card: " << testCard << std::endl;

    bool result1 = false;
    bool result2 = false;

    if (g_reader && g_reader->isOpen()) {
        result1 = g_reader->query(testCard);
        std::cout << "[TEST] PersistReader query result: " << (result1 ? "BLACKLISTED" : "NOT_BLACKLISTED") << std::endl;
    }

    if (g_checker) {
        result2 = g_checker->isBlacklisted(testCard);
        std::cout << "[TEST] BlacklistChecker query result: " << (result2 ? "BLACKLISTED" : "NOT_BLACKLISTED") << std::endl;
    }

    bool passed = result1 && result2;
    if (!passed) {
        std::cout << "[TEST] Expected BLACKLISTED for card: " << testCard << std::endl;
    }

    return passed;
}

bool testQueryNotBlacklisted() {
    std::cout << "\n[TEST] Testing NOT BLACKLISTED card query" << std::endl;

    std::string testCard = "11019999999999999999";
    std::cout << "[TEST] Query card: " << testCard << std::endl;

    bool result1 = true;
    bool result2 = true;

    if (g_reader && g_reader->isOpen()) {
        result1 = g_reader->query(testCard);
        std::cout << "[TEST] PersistReader query result: " << (result1 ? "BLACKLISTED" : "NOT_BLACKLISTED") << std::endl;
    }

    if (g_checker) {
        result2 = g_checker->isBlacklisted(testCard);
        std::cout << "[TEST] BlacklistChecker query result: " << (result2 ? "BLACKLISTED" : "NOT_BLACKLISTED") << std::endl;
    }

    bool passed = !result1 && !result2;
    if (!passed) {
        std::cout << "[TEST] Expected NOT_BLACKLISTED for card: " << testCard << std::endl;
    }

    return passed;
}

bool testDataConsistency() {
    std::cout << "\n[TEST] Testing data consistency" << std::endl;

    size_t checkerSize = 0;
    size_t readerSize = 0;

    if (g_checker) {
        checkerSize = g_checker->size();
        std::cout << "[TEST] BlacklistChecker size: " << checkerSize << std::endl;
    }

    if (g_reader && g_reader->isOpen()) {
        readerSize = g_reader->getTotalCards();
        std::cout << "[TEST] PersistReader size: " << readerSize << std::endl;
    }

    if (checkerSize != readerSize) {
        std::cout << "[TEST] WARNING: Sizes don't match! "
                  << "Checker: " << checkerSize << ", Reader: " << readerSize << std::endl;
    }

    return checkerSize == readerSize && checkerSize > 0;
}

size_t getLoadedCount() {
    return g_loadedCount;
}

void cleanupTest() {
    if (g_reader) {
        g_reader->close();
        delete g_reader;
        g_reader = nullptr;
    }
    if (g_checker) {
        delete g_checker;
        g_checker = nullptr;
    }
}