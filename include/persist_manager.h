/**
 * @file persist_manager.h
 * @brief 黑名单持久化管理器
 *
 * 负责黑名单数据的序列化和反序列化，实现：
 * - 从ZIP文件加载时创建缓存
 * - 启动时优先从缓存加载
 * - 支持按版本日期管理多个缓存
 */

#ifndef PERSIST_MANAGER_H
#define PERSIST_MANAGER_H

#include <string>
#include <cstdint>
#include <ctime>
#include <vector>

#include "blacklist_checker.h"

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    #include <shlobj.h>
#elif defined(__linux__)
    #include <sys/types.h>
    #include <unistd.h>
    #include <pwd.h>
#elif defined(__APPLE__)
    #include <mach-o/dyld.h>
    #include <sys/types.h>
    #include <sys/sysctl.h>
#endif

/**
 * @brief 缓存加载结果枚举
 */
enum class CacheLoadResult {
    SUCCESS,              // 加载成功
    NO_CACHE,            // 无缓存文件
    INVALID_HEADER,       // 无效的Header
    VERSION_MISMATCH,     // 版本不匹配
    CHECKSUM_FAILED,     // 校验失败
    DATA_CORRUPTED,      // 数据损坏
    FILE_IO_ERROR         // 文件IO错误
};

/**
 * @brief 缓存信息结构
 */
struct CacheInfo {
    std::string versionDate;      // 版本日期 "YYYYMMDD"
    std::string zipMd5;         // ZIP MD5
    uint64_t recordCount;        // 记录数
    QueryMode queryMode;         // 查询模式
    uint64_t createdTime;        // 创建时间
    uint32_t provinceCount;      // 省份数量
    uint64_t fileSize;           // 缓存文件大小
    std::string cachePath;       // 缓存文件路径
};

class PersistManager {
public:
    static const uint16_t CURRENT_VERSION = 1;
    static const char MAGIC[5];

    PersistManager();
    ~PersistManager();

    /**
     * @brief 从ZIP文件名提取版本日期
     * @param filename ZIP文件名
     * @return 版本日期字符串（如"20230620"），失败返回空字符串
     *
     * 命名格式：XXX_DownLoad_YYYYMMDD_XXX...
     * 示例：4401S0008440030010_DownLoad_20230620_CardBlacklistAll_...
     */
    static std::string extractVersionFromFilename(const std::string& filename);

    /**
     * @brief 获取缓存目录路径
     * @return 缓存目录路径
     */
    static std::string getCacheDirectory();

    /**
     * @brief 获取指定版本的缓存文件路径
     * @param versionDate 版本日期（如"20230620"）
     * @return 缓存文件完整路径
     */
    static std::string getCacheFilePath(const std::string& versionDate);

    /**
     * @brief 查找最新的缓存文件
     * @return 缓存文件路径，未找到返回空字符串
     */
    static std::string findLatestCache();

    /**
     * @brief 检查缓存是否可用
     * @param versionDate 版本日期（为空则查找最新）
     * @return 缓存加载结果
     */
    CacheLoadResult checkCacheAvailable(const std::string& versionDate = "");

    /**
     * @brief 获取缓存信息（不加载数据）
     * @param cachePath 缓存文件路径
     * @param info 输出：缓存信息
     * @return 是否成功
     */
    bool getCacheInfo(const std::string& cachePath, CacheInfo& info);

    /**
     * @brief 保存黑名单数据到缓存文件
     * @param zipPath ZIP文件路径（用于提取版本信息）
     * @param checker BlacklistChecker引用
     * @return 是否保存成功
     */
    bool save(const std::string& zipPath, BlacklistChecker& checker);

    /**
     * @brief 从缓存文件加载黑名单数据
     * @param cachePath 缓存文件路径
     * @param checker BlacklistChecker引用
     * @param mode 输出：缓存使用的查询模式
     * @return 加载结果
     */
    CacheLoadResult load(const std::string& cachePath,
                        BlacklistChecker& checker,
                        QueryMode& mode);

    /**
     * @brief 计算文件的MD5值
     * @param filePath 文件路径
     * @return MD5字符串（32字符），失败返回空字符串
     */
    static std::string calculateFileMd5(const std::string& filePath);

    /**
     * @brief 检查ZIP文件是否更新
     * @param cacheInfo 缓存信息
     * @param zipPath 当前ZIP文件路径
     * @return 是否需要重新加载
     */
    bool isCacheUpToDate(const CacheInfo& cacheInfo, const std::string& zipPath);

private:
    static std::string getHomeDirectory();
    bool createCacheDirectory();

    bool readHeader(const std::string& cachePath, BlacklistChecker::PersistHeader& header);
    bool writeHeader(std::ofstream& ofs, const std::string& zipPath,
                    BlacklistChecker& checker);
};

/**
 * @brief 从文件路径提取目录部分
 */
std::string extractDirectory(const std::string& filePath);

#endif // PERSIST_MANAGER_H
