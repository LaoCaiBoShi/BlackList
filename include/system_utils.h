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

#endif // SYSTEM_UTILS_H
