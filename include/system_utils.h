#ifndef SYSTEM_UTILS_H
#define SYSTEM_UTILS_H

#include <string>
#include <thread>

/**
 * @brief 获取CPU核心数
 * @return CPU核心数，最低返回4
 */
size_t getCpuCoreCount();

/**
 * @brief 计算最优队列大小
 * 根据系统内存动态计算队列大小：
 * - 16G及以上: 1000
 * - 8G: 500
 * - 4G: 300
 * - 更低: 200
 * @return 队列大小
 */
size_t calculateOptimalQueueSize();

/**
 * @brief 获取可用内存大小（MB）
 * @return 可用内存大小
 */
size_t getAvailableMemoryMB();

/**
 * @brief 检测磁盘是否为SSD
 * @param path 磁盘路径
 * @return 是否为SSD
 */
bool isSsdDrive(const std::string& path = ".");

/**
 * @brief 线程配置结构体
 */
struct ThreadConfig {
    size_t extractThreads;      // 解压线程数
    size_t parseThreads;        // JSON处理线程数
    size_t totalThreads;        // 总线程数
    size_t batchSize;           // 批处理大小
    size_t queueSize;           // 队列大小（根据内存动态：16G+=1000, 8G=500, 4G=300, <4G=200）
};

/**
 * @brief 布隆过滤器精度级别
 */
enum class BloomFilterPrecision {
    NORMAL = 0,      // 十万分之一 (10⁻⁵)
    HIGH = 1,        // 百万分之一 (10⁻⁶) - 默认
    ULTRA = 2       // 千万分之一 (10⁻⁷) - 不推荐，内存开销大
};

/**
 * @brief 布隆过滤器配置结构体
 */
struct BloomFilterConfig {
    BloomFilterPrecision precision;  // 精度级别
    double falsePositiveRate;       // 误判率

    // 精度级别对应的误判率
    static double getFalsePositiveRate(BloomFilterPrecision precision) {
        switch (precision) {
            case BloomFilterPrecision::NORMAL:  return 0.00001;   // 10⁻⁵ 十万分之一
            case BloomFilterPrecision::HIGH:  return 0.000001;  // 10⁻⁶ 百万分之一 (默认)
            case BloomFilterPrecision::ULTRA:  return 0.0000001; // 10⁻⁷ 千万分之一
            default: return 0.000001;
        }
    }

    // 获取精度级别名称
    static const char* getPrecisionName(BloomFilterPrecision precision) {
        switch (precision) {
            case BloomFilterPrecision::NORMAL:  return "NORMAL(十万分之一)";
            case BloomFilterPrecision::HIGH:   return "HIGH(百万分之一)";
            case BloomFilterPrecision::ULTRA:  return "ULTRA(千万分之一)";
            default: return "UNKNOWN";
        }
    }
};

/**
 * @brief 计算最优线程配置
 * @param provinceCount 省份数量（默认31）
 * @return 线程配置
 */
ThreadConfig calculateThreadConfig(size_t provinceCount = 31);

/**
 * @brief 性能统计结构体
 */
struct PerformanceStats {
    double extractSpeed;  // MB/s
    double parseSpeed;    // 条/秒
    double writeSpeed;    // 条/秒
    size_t memoryUsage;   // MB
};

/**
 * @brief 监控性能
 * @return 性能统计
 */
PerformanceStats monitorPerformance();

/**
 * @brief 调整线程配置
 * @param config 线程配置
 * @param stats 性能统计
 */
void adjustThreadConfig(ThreadConfig& config, const PerformanceStats& stats);

/**
 * @brief 获取降级配置
 * @return 降级配置
 */
ThreadConfig getFallbackConfig();

/**
 * @brief 检查文件是否有读取权限
 * @param filePath 文件路径
 * @return 是否有读取权限
 */
bool hasReadPermission(const std::string& filePath);

/**
 * @brief 检查是否为有效的ZIP文件（通过魔数校验）
 * @param filePath 文件路径
 * @return 是否为有效的ZIP文件
 */
bool isValidZipFile(const std::string& filePath);

/**
 * @brief 检查文件是否为空
 * @param filePath 文件路径
 * @return 文件大小是否为0
 */
bool isEmptyFile(const std::string& filePath);

/**
 * @brief 获取文件大小
 * @param filePath 文件路径
 * @return 文件大小（字节），失败返回0
 */
uint64_t getFileSizeSafe(const std::string& filePath);

/**
 * @brief 检查磁盘空间是否充足
 * @param path 目录路径
 * @param requiredBytes 需要的字节数
 * @return 空间是否充足
 */
bool checkDiskSpace(const std::string& path, uint64_t requiredBytes);

/**
 * @brief 统一验证ZIP文件（综合检查）
 * @param filePath 文件路径
 * @param errorMsg 错误信息输出
 * @return 是否验证通过
 */
bool validateZipFile(const std::string& filePath, std::string* errorMsg = nullptr);

#endif // SYSTEM_UTILS_H
