#include "system_utils.h"

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    #include <psapi.h>
    #include <winbase.h>
#elif defined(__linux__)
    #include <sys/sysinfo.h>
    #include <sys/stat.h>
    #include <unistd.h>
#elif defined(__APPLE__)
    #include <sys/sysctl.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

#include <iostream>
#include <chrono>

/**
 * @brief 获取CPU核心数
 */
size_t getCpuCoreCount() {
    size_t cpuCount = std::thread::hardware_concurrency();
    if (cpuCount == 0) {
#if defined(_WIN32) || defined(_WIN64)
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        cpuCount = sysInfo.dwNumberOfProcessors;
#elif defined(__linux__)
        cpuCount = sysconf(_SC_NPROCESSORS_ONLN);
#elif defined(__APPLE__)
        int ncpu;
        size_t size = sizeof(ncpu);
        sysctlbyname("hw.ncpu", &ncpu, &size, NULL, 0);
        cpuCount = ncpu;
#else
        cpuCount = 4;
#endif
    }
    if (cpuCount < 4) {
        cpuCount = 4; // 保底值
    }
    return cpuCount;
}

/**
 * @brief 获取可用内存大小（MB）
 */
size_t getAvailableMemoryMB() {
#if defined(_WIN32) || defined(_WIN64)
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);
    return memInfo.ullAvailPhys / (1024 * 1024);
#elif defined(__linux__)
    struct sysinfo info;
    sysinfo(&info);
    return info.freeram * info.mem_unit / (1024 * 1024);
#elif defined(__APPLE__)
    int64_t freeMem;
    size_t size = sizeof(freeMem);
    sysctlbyname("hw.memsize", &freeMem, &size, NULL, 0);
    return freeMem / (1024 * 1024);
#else
    return 4096; // 默认4GB
#endif
}

/**
 * @brief 检测磁盘是否为SSD
 */
bool isSsdDrive(const std::string& path) {
#if defined(_WIN32) || defined(_WIN64)
    BOOL result = FALSE;
    DWORD bytesReturned;
    STORAGE_PROPERTY_QUERY query;
    query.PropertyId = StorageDeviceSeekPenaltyProperty;
    query.QueryType = PropertyStandardQuery;
    
    HANDLE hDevice = CreateFileA(
        path.c_str(),
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );
    
    if (hDevice != INVALID_HANDLE_VALUE) {
        DEVICE_SEEK_PENALTY_DESCRIPTOR descriptor;
        if (DeviceIoControl(
            hDevice,
            IOCTL_STORAGE_QUERY_PROPERTY,
            &query,
            sizeof(query),
            &descriptor,
            sizeof(descriptor),
            &bytesReturned,
            NULL
        )) {
            result = descriptor.IncursSeekPenalty;
        }
        CloseHandle(hDevice);
    }
    return !result;
#elif defined(__linux__)
    std::string sysPath = "/sys/block/";
    // 简化实现：假设现代系统使用SSD
    return true;
#elif defined(__APPLE__)
    // 简化实现：假设现代系统使用SSD
    return true;
#else
    return true;
#endif
}

/**
 * @brief 计算最优解压线程数
 */
size_t calculateOptimalExtractThreads() {
    size_t cpuCount = getCpuCoreCount();
    bool isSsd = isSsdDrive(".");
    
    if (isSsd) {
        return std::min<size_t>(8, cpuCount);
    } else {
        return std::min<size_t>(4, cpuCount / 2);
    }
}

/**
 * @brief 计算最优批处理大小
 */
size_t calculateOptimalBatchSize() {
    size_t memoryAvailable = getAvailableMemoryMB();

    if (memoryAvailable >= 800) {
        return 10000;
    } else if (memoryAvailable >= 500) {
        return 5000;
    } else {
        return 1000;
    }
}

/**
 * @brief 根据系统内存计算最优队列大小
 * 
 * 内存与队列大小关系：
 * - 16G及以上: 1000 (预留充足缓冲)
 * - 8G: 500 (平衡配置)
 * - 4G: 300 (保守配置)
 * - 更低: 200 (极低内存)
 * 
 * 每个JsonTask约150KB，预估内存占用：
 * queueSize * 150KB = 总队列内存
 */
size_t calculateOptimalQueueSize() {
    size_t memoryMB = getAvailableMemoryMB();

    if (memoryMB >= 16000) {
        std::cout << "[Queue Config] Memory >= 16GB, using queue size: 1000" << std::endl;
        return 1000;
    } else if (memoryMB >= 8000) {
        std::cout << "[Queue Config] Memory >= 8GB, using queue size: 500" << std::endl;
        return 500;
    } else if (memoryMB >= 4000) {
        std::cout << "[Queue Config] Memory >= 4GB, using queue size: 300" << std::endl;
        return 300;
    } else {
        std::cout << "[Queue Config] Memory < 4GB, using queue size: 200" << std::endl;
        return 200;
    }
}

/**
 * @brief 计算最优线程配置（全局队列 + 消费者线程池模式）
 * @param provinceCount 实际省份数量
 */
ThreadConfig calculateThreadConfig(size_t provinceCount) {
    size_t cpuCount = getCpuCoreCount();
    size_t memoryMB = getAvailableMemoryMB();
    bool isSsd = isSsdDrive(".");

    ThreadConfig config;

    // 1. 解压线程数（IO密集型，SSD用8个，HDD用4个）
    if (isSsd) {
        config.extractThreads = std::min<size_t>(8UL, cpuCount / 4UL);
    } else {
        config.extractThreads = std::min<size_t>(4UL, cpuCount / 8UL);
    }
    if (config.extractThreads < 2) config.extractThreads = 2;  // 最少2个

    // 2. JSON处理线程数 = CPU核心数 - 解压线程数
    config.parseThreads = cpuCount - config.extractThreads;
    if (config.parseThreads < 2) config.parseThreads = 2;  // 最少2个

    // 3. 总线程数 = 解压 + 处理
    config.totalThreads = config.extractThreads + config.parseThreads;

    // 4. 队列大小：根据系统内存动态计算
    //    - 16G及以上: 1000
    //    - 8G: 500
    //    - 4G: 300
    //    - 更低: 200
    config.queueSize = calculateOptimalQueueSize();

    // 5. 批处理大小
    config.batchSize = calculateOptimalBatchSize();

    return config;
}

/**
 * @brief 监控性能
 */
PerformanceStats monitorPerformance() {
    PerformanceStats stats;
    
    // 1. 解压速度（模拟）
    // 实际实现需要记录解压时间和数据量
    stats.extractSpeed = 50.0; // MB/s
    
    // 2. JSON解析速度（模拟）
    // 实际实现需要记录解析时间和解析的条目数
    stats.parseSpeed = 50000.0; // 条/秒
    
    // 3. 写入速度（模拟）
    // 实际实现需要记录写入时间和写入的条目数
    stats.writeSpeed = 100000.0; // 条/秒
    
    // 4. 内存使用
    stats.memoryUsage = getAvailableMemoryMB();
    
    return stats;
}

/**
 * @brief 调整线程配置
 */
void adjustThreadConfig(ThreadConfig& config, const PerformanceStats& stats) {
    if (stats.extractSpeed < 10) {
        config.extractThreads = std::min<size_t>(8, config.extractThreads + 1);
    }

    if (stats.parseSpeed < 10000) {
        config.parseThreads = std::min<size_t>(config.totalThreads / 2, config.parseThreads + 1);
    }
}

/**
 * @brief 获取降级配置
 */
ThreadConfig getFallbackConfig() {
    ThreadConfig config;
    config.extractThreads = 2;
    config.parseThreads = 6;
    config.totalThreads = 8;
    config.batchSize = 1000;
    config.queueSize = calculateOptimalQueueSize();  // 使用动态计算
    return config;
}

/**
 * @brief 计算布隆过滤器配置
 * @param precision 精度级别
 * @param memoryMB 系统可用内存(MB)
 * @return 布隆过滤器配置
 */
BloomFilterConfig calculateBloomFilterConfig(BloomFilterPrecision precision,
                                             size_t memoryMB) {
    BloomFilterConfig config;
    config.precision = precision;
    config.falsePositiveRate = BloomFilterConfig::getFalsePositiveRate(precision);

    // 检查ULTRA模式内存限制
    if (precision == BloomFilterPrecision::ULTRA) {
        // ULTRA模式需要约750MB布隆过滤器内存
        if (memoryMB < 2000) {
            std::cerr << "[WARN] Memory " << memoryMB << "MB is too low for ULTRA precision, "
                      << "downgrading to HIGH precision" << std::endl;
            config.precision = BloomFilterPrecision::HIGH;
            config.falsePositiveRate = 0.000001;
        }
    }

    std::cout << "[BloomFilter Config] Precision: "
              << BloomFilterConfig::getPrecisionName(config.precision)
              << ", False Positive Rate: " << config.falsePositiveRate
              << std::endl;

    return config;
}
