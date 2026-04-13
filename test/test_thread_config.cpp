#include "system_utils.h"
#include <iostream>
#include <chrono>

int main() {
    std::cout << "=== Dynamic Thread Configuration Test ===" << std::endl;

    // 测试CPU核心数检测
    std::cout << "1. CPU Core Count: " << getCpuCoreCount() << std::endl;

    // 测试内存检测
    std::cout << "2. Available Memory (MB): " << getAvailableMemoryMB() << std::endl;

    // 测试磁盘类型检测
    std::cout << "3. Is SSD: " << (isSsdDrive() ? "Yes" : "No") << std::endl;

    // 测试线程配置计算
    ThreadConfig config = calculateThreadConfig();
    std::cout << "4. Thread Configuration:" << std::endl;
    std::cout << "   Extract Threads: " << config.extractThreads << std::endl;
    std::cout << "   Parse Threads: " << config.parseThreads << std::endl;
    std::cout << "   Total Threads: " << config.totalThreads << std::endl;
    std::cout << "   Batch Size: " << config.batchSize << std::endl;
    std::cout << "   Queue Size: " << config.queueSize << std::endl;

    // 测试性能监控
    PerformanceStats stats = monitorPerformance();
    std::cout << "5. Performance Stats:" << std::endl;
    std::cout << "   Extract Speed: " << stats.extractSpeed << " MB/s" << std::endl;
    std::cout << "   Parse Speed: " << stats.parseSpeed << " items/s" << std::endl;
    std::cout << "   Write Speed: " << stats.writeSpeed << " items/s" << std::endl;
    std::cout << "   Memory Usage: " << stats.memoryUsage << " MB" << std::endl;

    // 测试降级配置
    ThreadConfig fallback = getFallbackConfig();
    std::cout << "6. Fallback Configuration:" << std::endl;
    std::cout << "   Extract Threads: " << fallback.extractThreads << std::endl;
    std::cout << "   Parse Threads: " << fallback.parseThreads << std::endl;
    std::cout << "   Total Threads: " << fallback.totalThreads << std::endl;
    std::cout << "   Batch Size: " << fallback.batchSize << std::endl;
    std::cout << "   Queue Size: " << fallback.queueSize << std::endl;

    std::cout << "=== Test Completed ===" << std::endl;
    return 0;
}