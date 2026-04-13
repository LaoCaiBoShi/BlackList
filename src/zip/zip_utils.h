#ifndef ZIP_UTILS_H
#define ZIP_UTILS_H

#include <string>
#include "blacklist_checker.h"

/**
 * @brief 从压缩文件中加载黑名单
 * @param compressedPath 压缩文件路径
 * @param checker 黑名单检查器
 * @param totalLoaded 输出参数，加载的卡片数量
 * @param totalInvalid 输出参数，无效的卡片数量
 * @param baseDir 基础目录，可选，默认为空
 * @return 加载是否成功
 */
bool loadBlacklistFromCompressedFile(const std::string& compressedPath, BlacklistChecker& checker, size_t& totalLoaded, size_t& totalInvalid, const std::string& baseDir = "");

#endif // ZIP_UTILS_H
