#include "test.h"
#include "zip_utils.h"
#include <iostream>
#include <string>

void runTests(BlacklistChecker& checker)
{
    std::string path;
    std::cout << "Enter path: ";
    std::getline(std::cin, path);
    size_t loaded = 0;
    size_t invalid = 0;
    loadBlacklistFromCompressedFile(path, checker, loaded, invalid);
}