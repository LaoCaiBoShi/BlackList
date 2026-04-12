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

bool testPersistOnlyMode(const std::string& persistPath) {
    std::cout << "\n[TEST] Testing persist-only mode (mmap without ZIP): " << persistPath << std::endl;

    if (!PersistReader::isValid(persistPath)) {
        std::cout << "[TEST] Persist file is not valid" << std::endl;
        return false;
    }

    PersistReader* tempReader = new PersistReader();
    bool success = tempReader->open(persistPath);

    if (!success) {
        std::cout << "[TEST] Failed to open persist file with mmap" << std::endl;
        delete tempReader;
        return false;
    }

    size_t totalCards = tempReader->getTotalCards();
    size_t prefixCount = tempReader->getPrefixCount();

    std::cout << "[TEST] PersistReader: " << totalCards << " cards, " << prefixCount << " prefixes" << std::endl;

    if (totalCards == 0) {
        std::cout << "[TEST] ERROR: totalCards should not be 0" << std::endl;
        delete tempReader;
        return false;
    }

    std::string testCard = "11011201230003140853";
    bool queryResult = tempReader->query(testCard);
    std::cout << "[TEST] Query " << testCard << ": " << (queryResult ? "BLACKLISTED" : "NOT_BLACKLISTED") << std::endl;

    delete tempReader;
    return queryResult;
}

bool testQueryBlacklisted() {
    std::cout << "\n[TEST] Testing BLACKLISTED card query" << std::endl;

    std::string testCard = "11011201230003140853";
    std::cout << "[TEST] Query card: " << testCard << std::endl;

    bool readerResult = false;
    bool checkerResult = false;

    if (g_reader && g_reader->isOpen()) {
        readerResult = g_reader->query(testCard);
        std::cout << "[TEST] PersistReader query result: " << (readerResult ? "BLACKLISTED" : "NOT_BLACKLISTED") << std::endl;
    }

    if (g_checker && g_checker->size() > 0) {
        checkerResult = g_checker->isBlacklisted(testCard);
        std::cout << "[TEST] BlacklistChecker query result: " << (checkerResult ? "BLACKLISTED" : "NOT_BLACKLISTED") << std::endl;
    }

    if (!g_reader && !g_checker) {
        std::cout << "[TEST] No reader or checker available" << std::endl;
        return false;
    }

    if (g_reader && g_checker) {
        return readerResult && checkerResult;
    }

    return g_reader ? readerResult : checkerResult;
}

bool testQueryNotBlacklisted() {
    std::cout << "\n[TEST] Testing NOT BLACKLISTED card query" << std::endl;

    std::string testCard = "99999999999999999999";
    std::cout << "[TEST] Query card: " << testCard << std::endl;

    bool readerResult = true;
    bool checkerResult = true;

    if (g_reader && g_reader->isOpen()) {
        readerResult = g_reader->query(testCard);
        std::cout << "[TEST] PersistReader query result: " << (readerResult ? "BLACKLISTED" : "NOT_BLACKLISTED") << std::endl;
    }

    if (g_checker && g_checker->size() > 0) {
        checkerResult = g_checker->isBlacklisted(testCard);
        std::cout << "[TEST] BlacklistChecker query result: " << (checkerResult ? "BLACKLISTED" : "NOT_BLACKLISTED") << std::endl;
    }

    if (!g_reader && !g_checker) {
        std::cout << "[TEST] No reader or checker available" << std::endl;
        return false;
    }

    if (g_reader && g_checker) {
        return !readerResult && !checkerResult;
    }

    return g_reader ? !readerResult : !checkerResult;
}

bool testMultipleBlacklistedCards() {
    std::cout << "\n[TEST] Testing multiple blacklisted cards" << std::endl;

    std::vector<std::string> blacklistedCards = {
        "11011201230003140853"
    };

    bool allFound = true;

    for (const auto& card : blacklistedCards) {
        bool result1 = false;
        bool result2 = false;

        if (g_reader && g_reader->isOpen()) {
            result1 = g_reader->query(card);
        }

        if (g_checker && g_checker->size() > 0) {
            result2 = g_checker->isBlacklisted(card);
        }

        std::cout << "[TEST] Card " << card << ": Reader=" << result1
                  << ", Checker=" << result2 << std::endl;

        if (!result1) {
            std::cout << "[TEST] WARNING: Card not found in PersistReader" << std::endl;
            allFound = false;
        }
    }

    return allFound;
}

bool testCardFieldExtraction() {
    std::cout << "\n[TEST] Testing card field extraction" << std::endl;

    BlacklistChecker checker;

    struct TestCase {
        std::string cardId;
        unsigned short expectedPrefix;
        unsigned short expectedType;
        unsigned short expectedYear;
        unsigned short expectedWeek;
    };

    std::vector<TestCase> testCases = {
        {"11011201230003140853", 1101, 23, 12, 1},
        {"44010608220012720100", 4401, 22, 6, 8}
    };

    bool allPassed = true;

    for (const auto& tc : testCases) {
        unsigned short prefix = checker.getPrefixCode(tc.cardId);
        unsigned short type = checker.getCardType(tc.cardId);
        unsigned short year = checker.getYear(tc.cardId);
        unsigned short week = checker.getWeek(tc.cardId);

        std::cout << "[TEST] Card: " << tc.cardId << std::endl;
        std::cout << "[TEST]   Expected: prefix=" << tc.expectedPrefix
                  << ", type=" << tc.expectedType
                  << ", year=" << tc.expectedYear
                  << ", week=" << tc.expectedWeek << std::endl;
        std::cout << "[TEST]   Got:      prefix=" << prefix
                  << ", type=" << type
                  << ", year=" << year
                  << ", week=" << week << std::endl;

        if (prefix != tc.expectedPrefix || type != tc.expectedType ||
            year != tc.expectedYear || week != tc.expectedWeek) {
            std::cout << "[TEST]   FAILED!" << std::endl;
            allPassed = false;
        } else {
            std::cout << "[TEST]   PASSED" << std::endl;
        }
    }

    return allPassed;
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

    if (checkerSize != readerSize && checkerSize > 0 && readerSize > 0) {
        std::cout << "[TEST] WARNING: Sizes don't match! "
                  << "Checker: " << checkerSize << ", Reader: " << readerSize << std::endl;
        return false;
    }

    return (checkerSize > 0 || readerSize > 0);
}

bool testRestartRecovery(const std::string& persistPath) {
    std::cout << "\n[TEST] Testing restart recovery from: " << persistPath << std::endl;

    std::cout << "[TEST] Simulating program restart (cleaning up)..." << std::endl;
    cleanupTest();

    std::cout << "[TEST] Creating new instance after restart..." << std::endl;
    g_checker = new BlacklistChecker();

    std::cout << "[TEST] Loading directly from persist file..." << std::endl;
    if (!g_checker->loadFromPersistFile(persistPath)) {
        std::cout << "[TEST] Failed to load from persist file" << std::endl;
        return false;
    }

    size_t checkerSize = g_checker->size();
    std::cout << "[TEST] Restored " << checkerSize << " records from persist file" << std::endl;

    std::string testCard = "11011201230003140853";
    std::cout << "[TEST] Querying card after restart: " << testCard << std::endl;
    bool queryResult = g_checker->isBlacklisted(testCard);
    std::cout << "[TEST] Query result: " << (queryResult ? "BLACKLISTED" : "NOT_BLACKLISTED") << std::endl;

    std::string testCard2 = "11019999999999999999";
    std::cout << "[TEST] Querying non-blacklisted card: " << testCard2 << std::endl;
    bool queryResult2 = g_checker->isBlacklisted(testCard2);
    std::cout << "[TEST] Query result: " << (queryResult2 ? "BLACKLISTED" : "NOT_BLACKLISTED") << std::endl;

    return checkerSize > 0 && queryResult && !queryResult2;
}

bool testPersistReaderQuery(const std::string& persistPath) {
    std::cout << "\n[TEST] Testing PersistReader direct query: " << persistPath << std::endl;

    cleanupTest();

    g_reader = new PersistReader();

    if (!PersistReader::isValid(persistPath)) {
        std::cout << "[TEST] Persist file is not valid" << std::endl;
        return false;
    }

    if (!g_reader->open(persistPath)) {
        std::cout << "[TEST] Failed to open persist file" << std::endl;
        return false;
    }

    std::cout << "[TEST] PersistReader opened: " << g_reader->getTotalCards()
              << " cards, " << g_reader->getPrefixCount() << " prefixes" << std::endl;

    std::string blacklistedCard = "11011201230003140853";
    std::string nonBlacklistedCard = "99999999999999999999";

    bool blResult = g_reader->query(blacklistedCard);
    bool nblResult = g_reader->query(nonBlacklistedCard);

    std::cout << "[TEST] Blacklisted card " << blacklistedCard
              << ": " << (blResult ? "BLACKLISTED" : "NOT_BLACKLISTED") << std::endl;
    std::cout << "[TEST] Non-blacklisted card " << nonBlacklistedCard
              << ": " << (nblResult ? "BLACKLISTED" : "NOT_BLACKLISTED") << std::endl;

    return blResult && !nblResult;
}

bool testInnerIdEncoding() {
    std::cout << "\n[TEST] Testing innerId encoding/decoding consistency" << std::endl;

    BlacklistChecker checker;

    std::vector<std::pair<std::string, std::string>> testCases = {
        {"0003140853", "11011201230003140853"},
        {"0012720100", "11010808220012720100"}
    };

    bool allPassed = true;

    for (const auto& tc : testCases) {
        std::string innerIdStr = tc.first;
        std::string fullCardId = tc.second;

        unsigned short prefix = checker.getPrefixCode(fullCardId);
        unsigned short year = checker.getYear(fullCardId);
        unsigned short week = checker.getWeek(fullCardId);
        unsigned short type = checker.getCardType(fullCardId);

        BlacklistChecker::CardInfo info(year, week, innerIdStr);
        std::string decodedInnerId = info.getInnerIdStr();

        std::cout << "[TEST] InnerId: " << innerIdStr
                  << " -> CardInfo -> getInnerIdStr(): " << decodedInnerId << std::endl;

        if (innerIdStr != decodedInnerId) {
            std::cout << "[TEST] FAILED: InnerId mismatch!" << std::endl;
            allPassed = false;
        } else {
            std::cout << "[TEST] PASSED" << std::endl;
        }

        std::string reconstructedCardId = std::to_string(prefix);
        std::string yearStr = std::to_string(year);
        if (yearStr.length() < 2) yearStr = "0" + yearStr;
        reconstructedCardId += yearStr;
        std::string weekStr = std::to_string(week);
        if (weekStr.length() < 2) weekStr = "0" + weekStr;
        reconstructedCardId += weekStr;
        reconstructedCardId += std::to_string(type);
        if (reconstructedCardId.length() < 10) {
            reconstructedCardId = std::string(10 - reconstructedCardId.length(), '0') + reconstructedCardId;
        }
        reconstructedCardId += decodedInnerId;

        std::cout << "[TEST] Reconstructed card: " << reconstructedCardId << std::endl;
        std::cout << "[TEST] Original card:      " << fullCardId << std::endl;

        if (reconstructedCardId != fullCardId) {
            std::cout << "[TEST] FAILED: CardId mismatch!" << std::endl;
            allPassed = false;
        } else {
            std::cout << "[TEST] PASSED" << std::endl;
        }
    }

    return allPassed;
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