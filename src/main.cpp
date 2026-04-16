#define _CRT_SECURE_NO_WARNINGS
#include "blacklist_service.h"
#include "system_utils.h"
#include "persist_manager.h"
#include "log_manager.h"
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

QueryMode selectQueryMode() {
    std::cout << "\n==========================================" << std::endl;
    std::cout << "Select Query Mode" << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "1. BLOOM_ONLY (150MB, ~0.001% false positive)" << std::endl;
    std::cout << "   - Only uses Bloom filter" << std::endl;
    std::cout << "   - Fastest, lowest memory" << std::endl;
    std::cout << "   - May have false positives" << std::endl;
    std::cout << std::endl;
    std::cout << "2. CARDINFO_ONLY (350MB, 0% false positive) [DEFAULT]" << std::endl;
    std::cout << "   - Only uses CardInfo storage" << std::endl;
    std::cout << "   - Moderate memory" << std::endl;
    std::cout << "   - 100% accurate" << std::endl;
    std::cout << std::endl;
    std::cout << "3. BLOOM_AND_CARDINFO (500MB, 0% false positive)" << std::endl;
    std::cout << "   - Uses both Bloom filter and CardInfo" << std::endl;
    std::cout << "   - Best performance balance" << std::endl;
    std::cout << "   - 100% accurate" << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "Enter choice (1/2/3) or mode name [default: 2]: ";

    std::string input;
    std::getline(std::cin, input);
    input.erase(0, input.find_first_not_of(" \t\r\n"));

    QueryMode mode;
    if (input.empty() || input == "2" || input == "cardinfo" || input == "CARDINFO_ONLY") {
        mode = QueryMode::CARDINFO_ONLY;
    } else if (input == "1" || input == "bloom" || input == "BLOOM_ONLY") {
        mode = QueryMode::BLOOM_ONLY;
    } else if (input == "3" || input == "full" || input == "BLOOM_AND_CARDINFO") {
        mode = QueryMode::BLOOM_AND_CARDINFO;
    } else {
        std::cout << "Invalid input, using default: CARDINFO_ONLY" << std::endl;
        mode = QueryMode::CARDINFO_ONLY;
    }

    const char* modeNames[] = {"BLOOM_ONLY", "CARDINFO_ONLY", "BLOOM_AND_CARDINFO"};
    LOG_INFO("Query mode selected: %s", modeNames[static_cast<int>(mode)]);

    return mode;
}

void queryCardLoop(BlacklistService& service, PersistManager& pm) {
    std::cout << "\n==========================================" << std::endl;
    std::cout << "Blacklist Query Mode (Console)" << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "Status: " << service.getStatusString() << std::endl;
    std::cout << "Loaded: " << service.getBlacklistSize() << " records" << std::endl;
    std::cout << "\nCommands:" << std::endl;
    std::cout << "  - Enter card ID to check (20 digits)" << std::endl;
    std::cout << "  - 'status' : Show service status and version" << std::endl;
    std::cout << "  - 'update'  : Reload blacklist from new ZIP file" << std::endl;
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
            LOG_INFO("User requested exit via quit command");
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

        if (input == "update" || input == "reload") {
            std::cout << "\n--- Update Blacklist ---" << std::endl;
            std::cout << "Enter new ZIP file path: ";
            std::string newZipPath;
            std::getline(std::cin, newZipPath);

            newZipPath.erase(0, newZipPath.find_first_not_of(" \t\r\n"));
            newZipPath.erase(newZipPath.find_last_not_of(" \t\r\n") + 1);

            if (newZipPath.empty()) {
                std::cout << "Update cancelled: empty path" << std::endl;
                LOG_INFO("Update cancelled: empty path");
                continue;
            }

            if (!std::filesystem::exists(newZipPath)) {
                std::cout << "Update failed: file not found: " << newZipPath << std::endl;
                LOG_ERROR("Update failed: file not found: %s", newZipPath.c_str());
                continue;
            }

            std::string validationError;
            if (!validateZipFile(newZipPath, &validationError)) {
                std::cout << "Update failed: invalid ZIP file" << std::endl;
                std::cout << "Reason: " << validationError << std::endl;
                LOG_ERROR("Update failed: invalid ZIP: %s", validationError.c_str());
                continue;
            }

            QueryMode newMode = selectQueryMode();

            std::cout << "\nLoading new blacklist..." << std::endl;
            LOG_INFO("Starting blacklist update from: %s", newZipPath.c_str());

            if (service.update(newZipPath, newMode)) {
                std::cout << "\nUpdate successful!" << std::endl;
                std::cout << "Records: " << service.getBlacklistSize() << std::endl;
                LOG_INFO("Blacklist update completed successfully");

                std::string versionDate = PersistManager::extractVersionFromFilename(newZipPath);
                if (!versionDate.empty()) {
                    std::string cachePath = PersistManager::getCacheFilePath(versionDate);
                    std::cout << "\nSaving cache to: " << cachePath << std::endl;
                    LOG_INFO("Saving updated cache to: %s", cachePath.c_str());

                    if (pm.createCacheDirectory()) {
                        if (service.saveToPersistFile(cachePath)) {
                            std::cout << "[OK] Cache updated successfully" << std::endl;
                            LOG_INFO("Cache updated successfully");
                        } else {
                            std::cerr << "[ERROR] Cache update failed" << std::endl;
                            LOG_ERROR("Cache update failed");
                        }
                    }
                }
            } else {
                std::cout << "\nUpdate failed: " << service.getLastError() << std::endl;
                LOG_ERROR("Blacklist update failed: %s", service.getLastError().c_str());
            }
            continue;
        }

        if (input.length() == 20) {
            LOG_INFO("Card query request: %.20s", input.c_str());
            QueryResult result = service.checkCard(input);

            const char* modeTag;
            switch (result.method) {
                case QueryMethod::BLOOM_FAST_REJECT:
                    modeTag = "(BLOOM_FAST_REJECT)";
                    break;
                case QueryMethod::BLOOM_CONTAINED:
                    modeTag = "(BLOOM_ONLY)";
                    break;
                case QueryMethod::CARDINFO_EXACT:
                default:
                    modeTag = "(CARDINFO)";
                    break;
            }

            std::cout << "Result " << modeTag << ": "
                      << (result.isBlacklisted ? "BLACKLISTED" : "NOT BLACKLISTED")
                      << std::endl;

            LOG_INFO("Card query result: %.20s -> %s %s",
                     input.c_str(),
                     result.isBlacklisted ? "BLACKLISTED" : "NOT BLACKLISTED",
                     modeTag);
        } else {
            std::cout << "Invalid card ID: must be 20 digits" << std::endl;
            LOG_WARN("Invalid card ID format: length=%zu", input.length());
        }
    }
}

int main(int argc, char* argv[]) {
    auto programStartTime = std::chrono::steady_clock::now();

    std::cout << "==========================================" << std::endl;
    std::cout << "Blacklist Service - Startup" << std::endl;
    std::cout << "==========================================" << std::endl;

    LogManager::getInstance().init("D:/Coder/BlackList/logs", LogManager::INFO);
    LOG_INFO("==========================================");
    LOG_INFO("Blacklist Service starting...");

    PersistManager pm;

    std::string cacheDir = PersistManager::getCacheDirectory();
    LOG_DEBUG("Cache directory: %s", cacheDir.c_str());

    std::string latestCache = PersistManager::findLatestCache();
    LOG_DEBUG("Latest cache path: %s", latestCache.empty() ? "(none)" : latestCache.c_str());

    CacheInfo cacheInfo;
    auto cacheResult = pm.checkCacheAvailable();
    if (cacheResult == CacheLoadResult::SUCCESS) {
        std::string latestCache = PersistManager::findLatestCache();
        if (pm.getCacheInfo(latestCache, cacheInfo)) {
            std::cout << "\n==========================================" << std::endl;
            std::cout << "Cache Found!" << std::endl;
            std::cout << "==========================================" << std::endl;
            std::cout << "Cache version: " << cacheInfo.versionDate << std::endl;
            std::cout << "Records: " << cacheInfo.recordCount << std::endl;
            std::cout << "Cache file: " << cacheInfo.cachePath << std::endl;
            std::cout << "\nLoading from cache (fast mode)..." << std::endl;
            LOG_INFO("Cache found: %s, records: %llu",
                     cacheInfo.versionDate.c_str(),
                     (unsigned long long)cacheInfo.recordCount);

            BlacklistService service(QueryMode::CARDINFO_ONLY);
            auto loadResult = service.loadFromPersistFile(latestCache);

            if (loadResult) {
                std::cout << "\nCache loaded successfully!" << std::endl;
                std::cout << "Records: " << service.getBlacklistSize() << std::endl;
                LOG_INFO("Cache loaded successfully, records: %lld",
                         (long long)service.getBlacklistSize());

                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - programStartTime).count();
                std::cout << "Load time: " << duration << " ms" << std::endl;

                queryCardLoop(service, pm);

                auto endTime = std::chrono::steady_clock::now();
                auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    endTime - programStartTime).count();
                LOG_INFO("Program exiting normally, total runtime: %lld ms", (long long)totalDuration);
                std::cout << "Total runtime: " << totalDuration << " ms" << std::endl;
                return 0;
            } else {
                std::cout << "\nCache load failed, falling back to ZIP mode..." << std::endl;
                LOG_WARN("Cache load failed, falling back to ZIP mode");
            }
        }
    } else {
        std::cout << "\nNo cache found, will load from ZIP file." << std::endl;
        LOG_INFO("No cache found");
    }

    std::string zipPath;
    if (argc > 1) {
        zipPath = argv[1];
        std::cout << "ZIP file: " << zipPath << std::endl;
        LOG_INFO("ZIP file from command line: %s", zipPath.c_str());
    } else {
        std::cout << "\nEnter compressed file path: ";
        std::getline(std::cin, zipPath);
        LOG_INFO("ZIP file from stdin: %s", zipPath.empty() ? "(empty)" : zipPath.c_str());
    }

    while (true) {
        if (zipPath.empty()) {
            std::cout << "\nError: No ZIP file specified" << std::endl;
            LOG_WARN("No ZIP file specified");
        } else if (!std::filesystem::exists(zipPath)) {
            std::cout << "\n==========================================" << std::endl;
            std::cout << "Error: File not found" << std::endl;
            std::cout << "==========================================" << std::endl;
            std::cout << "The specified file does not exist: " << zipPath << std::endl;
            LOG_ERROR("File not found: %s", zipPath.c_str());
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
            std::string validationError;
            if (!validateZipFile(zipPath, &validationError)) {
                std::cout << "\n==========================================" << std::endl;
                std::cout << "Error: Invalid ZIP file" << std::endl;
                std::cout << "==========================================" << std::endl;
                std::cout << "Validation failed: " << validationError << std::endl;
                std::cout << "\nPossible causes:" << std::endl;
                std::cout << "  - File is empty" << std::endl;
                std::cout << "  - File is not a valid ZIP format" << std::endl;
                std::cout << "  - No read permission" << std::endl;
                std::cout << "==========================================" << std::endl;
            } else {
                QueryMode mode = selectQueryMode();

                const char* modeName[] = {"BLOOM_ONLY", "CARDINFO_ONLY", "BLOOM_AND_CARDINFO"};
                std::cout << "Selected mode: " << modeName[static_cast<int>(mode)] << std::endl;
                LOG_INFO("User selected mode: %s", modeName[static_cast<int>(mode)]);

                LOG_INFO("File validation passed: %s", zipPath.c_str());
                LOG_INFO("Initializing BlacklistService...");
                BlacklistService service(mode);
                bool initSuccess = service.initialize(zipPath, mode);

                std::cout << "\n==========================================" << std::endl;
                std::cout << "Initialization Result" << std::endl;
                std::cout << "==========================================" << std::endl;
                std::cout << "Status: " << service.getStatusString() << std::endl;
                std::cout << "Loaded count: " << service.getBlacklistSize() << std::endl;

                if (initSuccess && service.getBlacklistSize() > 0) {
                    std::cout << "\nBlacklist loaded successfully!" << std::endl;
                    LOG_INFO("BlacklistService initialized successfully, records: %lld",
                             (long long)service.getBlacklistSize());

                    std::string versionDate = PersistManager::extractVersionFromFilename(zipPath);
                    if (!versionDate.empty()) {
                        std::string cachePath = PersistManager::getCacheFilePath(versionDate);
                        std::cout << "\nSaving cache to: " << cachePath << std::endl;
                        LOG_INFO("Saving cache to: %s", cachePath.c_str());

                        if (!pm.createCacheDirectory()) {
                            LOG_ERROR("Failed to create cache directory");
                            std::cerr << "[ERROR] Cache directory creation failed!" << std::endl;
                        } else if (!service.saveToPersistFile(cachePath)) {
                            LOG_ERROR("Failed to save cache to: %s", cachePath.c_str());
                            std::cerr << "[ERROR] Cache save failed!" << std::endl;
                        } else {
                            LOG_INFO("Cache save completed successfully");
                            std::cout << "[OK] Cache saved successfully" << std::endl;
                        }
                    } else {
                        LOG_WARN("Cannot extract version from filename, skipping cache save");
                        std::cerr << "[WARN] Cannot extract version from filename: " << zipPath << std::endl;
                    }

                    queryCardLoop(service, pm);
                } else {
                    std::cout << "\nInitialization failed. Exiting..." << std::endl;
                    std::cout << "Error: " << service.getLastError() << std::endl;
                    LOG_ERROR("Initialization failed: %s", service.getLastError().c_str());
                    return 1;
                }

                auto endTime = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    endTime - programStartTime).count();
                LOG_INFO("Program exiting normally, total runtime: %lld ms", (long long)duration);
                std::cout << "Total runtime: " << duration << " ms" << std::endl;

                return 0;
            }
        }

        std::cout << "\nPlease enter a valid ZIP file path (or 'quit' to exit): ";
        std::getline(std::cin, zipPath);

        zipPath.erase(0, zipPath.find_first_not_of(" \t\r\n"));
        size_t endPos = zipPath.find_last_not_of(" \t\r\n");
        if (endPos != std::string::npos) {
            zipPath.erase(endPos + 1);
        } else {
            zipPath.clear();
        }

        if (!zipPath.empty() && (zipPath == "quit" || zipPath == "q" || zipPath == "exit")) {
            std::cout << "Exiting..." << std::endl;
            LOG_INFO("User exited at file input prompt");
            return 0;
        }
    }
}
