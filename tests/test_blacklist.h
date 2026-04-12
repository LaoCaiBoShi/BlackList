/**
 * @file test_blacklist.h
 * @brief 黑名单加载测试
 */

#ifndef TEST_BLACKLIST_H
#define TEST_BLACKLIST_H

#include <string>

/**
 * @brief 测试从ZIP文件加载黑名单
 * @param zipPath ZIP文件路径
 * @return 是否成功
 */
bool testLoadFromZip(const std::string& zipPath);

/**
 * @brief 测试持久化保存
 * @param persistPath 持久化文件路径
 * @return 是否成功
 */
bool testPersistSave(const std::string& persistPath);

/**
 * @brief 测试持久化加载
 * @param persistPath 持久化文件路径
 * @return 是否成功
 */
bool testPersistLoad(const std::string& persistPath);

/**
 * @brief 测试纯持久化模式（mmap-only，无ZIP加载）
 * @param persistPath 持久化文件路径
 * @return 是否成功
 */
bool testPersistOnlyMode(const std::string& persistPath);

/**
 * @brief 测试黑名单查询（应该返回BLACKLISTED）
 * @return 是否成功
 */
bool testQueryBlacklisted();

/**
 * @brief 测试黑名单查询（应该返回NOT_BLACKLISTED）
 * @return 是否成功
 */
bool testQueryNotBlacklisted();

/**
 * @brief 测试多个黑名单卡号查询
 * @return 是否成功
 */
bool testMultipleBlacklistedCards();

/**
 * @brief 测试卡号字段提取（类型、年份、星期）
 * @return 是否成功
 */
bool testCardFieldExtraction();

/**
 * @brief 测试数据一致性
 * @return 是否成功
 */
bool testDataConsistency();

/**
 * @brief 测试重启后持久化恢复
 * @param persistPath 持久化文件路径
 * @return 是否成功
 */
bool testRestartRecovery(const std::string& persistPath);

/**
 * @brief 测试 PersistReader 直接查询
 * @param persistPath 持久化文件路径
 * @return 是否成功
 */
bool testPersistReaderQuery(const std::string& persistPath);

/**
 * @brief 测试内部ID编码/解码一致性
 * @return 是否成功
 */
bool testInnerIdEncoding();

/**
 * @brief 获取加载的记录数
 */
size_t getLoadedCount();

/**
 * @brief 清理测试环境
 */
void cleanupTest();

#endif // TEST_BLACKLIST_H