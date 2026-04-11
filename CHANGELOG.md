# 黑名单系统变更记录

## 概述

本文档记录黑名单系统的每次重要变更，包括功能新增、性能优化和问题修复。

---

## [待发布] 功能增强版本

### Commit 5: PersistReader mmap 内存映射优化

**日期**: 2026-04-11

**变更内容**:
- 新增 `PersistReader` 类，实现跨平台 mmap 内存映射
- Windows 实现：CreateFileMapping + MapViewOfFile
- Linux 实现：mmap + munmap
- 将 CardInfo 结构公开给 PersistReader（友元类）
- BlacklistService 集成 PersistReader，查询直接通过 mmap 执行
- 修复 persist_reader.cpp 中类型名限定符问题
- 修复 isValid() 方法资源泄漏（文件未关闭）
- 后台加载完成后重新打开 mmap

**新增文件**:
- `include/persist_reader.h` - PersistReader 类声明
- `src/core/persist_reader.cpp` - PersistReader 类实现

**修改文件**:
- `include/blacklist_checker.h` - 添加 PersistReader 友元声明，公开 CardInfo
- `include/blacklist_service.h` - 添加 persistReader_ 成员
- `src/core/blacklist_service.cpp` - 集成 mmap 查询逻辑

**技术细节**:
- mmap 查询：零拷贝直接访问持久化文件内存
- 启动速度：从持久化恢复 < 10 秒
- 查询路径：PersistReader::query() → 二分查找 mmap 内存

---

### Commit 1: 持久化功能基础

**日期**: 2026-04-11

**变更内容**:
- 新增持久化文件格式定义（Header + IndexTable + BinaryCardData）
- 实现 `saveToPersistFile()` 方法：保存为紧凑二进制格式
- 实现 `loadFromPersistFile()` 方法：从持久化文件快速加载
- 实现 `savePersistAfterLoad()` 方法：在数据加载后保存快照
- 实现 `isPersistFileValid()` 方法：检查持久化文件有效性
- 添加 `CardInfo` 默认构造函数，解决 vector resize 问题
- 统一所有加载入口（loadFromFile、updateFromFile、loadTxtBlacklist、loadFromJsonFile）调用 sortAll()

**修改文件**:
- `include/blacklist_checker.h` - 添加持久化接口声明、CardInfo 默认构造函数
- `src/core/blacklist_checker.cpp` - 实现持久化功能、添加 sortAll() 调用

**技术细节**:
- 持久化文件格式版本：1
- Header 大小：64 字节
- 每个省份索引条目：32 字节
- CardInfo 二进制：10 字节/条

---

### Commit 2: BlacklistService 服务封装

**日期**: 2026-04-11

**变更内容**:
- 新增 `BlacklistService` 服务封装类
- 实现服务状态管理（UNINITIALIZED/LOADING/READY/UPDATING/ERROR）
- 实现优先持久化恢复，后台异步加载新数据
- 实现 `triggerUpdate()` 触发后台异步更新
- 实现 `syncUpdate()` 同步更新（阻塞）
- 实现 `waitForReady()` 等待服务就绪

**新增文件**:
- `include/blacklist_service.h` - BlacklistService 类声明
- `src/core/blacklist_service.cpp` - BlacklistService 类实现
- `CMakeLists.txt` - 添加 blacklist_service.cpp 到编译

**技术细节**:
- 使用 `std::unique_ptr<BlacklistChecker>` 避免 mutex 复制问题
- 后台加载使用独立线程，不阻塞查询
- 数据切换使用原子操作保证一致性

---

### Commit 3: queryCardLoop 更新命令支持

**日期**: 2026-04-11

**变更内容**:
- 重构 `queryCardLoop()` 支持多种命令
- 新增 `update` 命令：触发后台异步更新
- 新增 `help` 命令：显示帮助信息
- 新增 `status` 命令别名（stat/s）
- 新增卡号长度验证（20位）
- 优化提示信息，显示可用命令列表

**修改文件**:
- `src/main.cpp` - queryCardLoop 函数重构

---

### Commit 4: main.cpp 优先持久化恢复逻辑

**日期**: 2026-04-11

**变更内容**:
- 程序启动时优先检查持久化文件
- 有效持久化文件存在时，直接快速恢复，无需输入 ZIP 路径
- 无持久化文件时，提示用户输入 ZIP 路径
- 启动后显示当前持久化文件状态

**修改文件**:
- `src/main.cpp` - main 函数启动逻辑

---

## 技术架构

### 数据流

```
程序启动
    │
    ▼
┌─────────────────┐
│ 检查持久化文件   │
└────────┬────────┘
         │
    ┌────┴────┐
    │         │
  有效      无效
    │         │
    ▼         ▼
秒级恢复    等待输入
    │         ZIP
    │         │
    │◄────────┘
    │
    ▼
启动后台线程
加载新ZIP
    │
    ▼
进入查询模式
（可响应update命令）
```

### 内存占用

| 组件 | 大小 |
|------|------|
| CardInfo 数据（6000万条） | ~572 MB |
| 布隆过滤器（20亿bits） | ~240 MB |
| 索引结构 | ~1 MB |
| 预分配余量 | ~50 MB |
| **总计** | **< 800 MB** |

### 性能指标

| 操作 | 时间 |
|------|------|
| 首次加载（从ZIP） | 10-15 分钟 |
| 持久化文件保存 | 1-2 分钟 |
| **从持久化恢复** | **< 10 秒** |
| 后台异步更新 | 不阻塞查询 |

---

## 命令行使用

```bash
# 首次运行（需要ZIP）
./blacklist_checker /path/to/blacklist.zip

# 后续运行（只需持久化文件）
./blacklist_checker

# 指定端口和持久化文件路径
./blacklist_checker /path/to/zip port persist.dat
```

### 交互式命令

```
> <20位卡号>    - 查询黑名单
> status        - 显示服务状态
> update        - 触发后台更新
> help          - 显示帮助
> quit          - 退出
```

---

## 未来优化方向

1. **mmap 内存映射**：使用 mmap 实现更快的持久化加载
2. **增量更新**：支持 delta 更新包，减少更新时间
3. **多级缓存**：热点数据 LRU 缓存
4. **分布式支持**：支持多实例共享黑名单
