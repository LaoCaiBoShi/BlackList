# 解压线程 + 多线程JSON处理方案

## 方案设计

```
┌─────────────────────────────────────────────────────────────────┐
│                                                                  │
│  解压线程池（N个）              JSON处理线程池（M个）            │
│  ┌─────────────────┐         ┌─────────────────┐               │
│  │ 线程1: 解压省44 │ ──queue─▶│ 线程1: 解析JSON │               │
│  │ 线程2: 解压省31 │ ──queue─▶│ 线程2: 解析JSON │               │
│  │ 线程3: 解压省11 │ ──queue─▶│ 线程3: 解析JSON │               │
│  │ ...             │ ──queue─▶│ ...             │               │
│  └─────────────────┘         └─────────────────┘               │
│                                                                  │
│  队列存储：JSON文件内容（字符串）                                │
│  队列容量：可调（控制内存）                                      │
└─────────────────────────────────────────────────────────────────┘
```

---

## 方案架构

### 三层架构

```
┌─────────────────────────────────────────────────────────────────┐
│  第一层：解压线程（IO密集型）                                    │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │ 线程1 ──▶ 解压JSON_1 ──▶ 入队                          │   │
│  │ 线程2 ──▶ 解压JSON_2 ──▶ 入队                          │   │
│  │ ...                                                      │   │
│  │ 线程N ──▶ 解压JSON_N ──▶ 入队                          │   │
│  └─────────────────────────────────────────────────────────┘   │
└───────────────────────────────┬─────────────────────────────────┘
                                │ 队列（JSON内容）
┌───────────────────────────────▼─────────────────────────────────┐
│  第二层：JSON处理线程（CPU密集型）                               │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │ 线程1 ◀── 出队 ──▶ 解析 ──▶ 添加到存储                 │   │
│  │ 线程2 ◀── 出队 ──▶ 解析 ──▶ 添加到存储                 │   │
│  │ ...                                                      │   │
│  │ 线程M ◀── 出队 ──▶ 解析 ──▶ 添加到存储                 │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

---

## 线程配置建议

| 配置         | 解压线程 | JSON处理线程 | 总线程 | 适用场景         |
| ------------ | -------- | ------------ | ------ | ---------------- |
| **平衡配置** | 4        | 28           | 32     | SSD，兼顾IO和CPU |
| **IO优先**   | 8        | 24           | 32     | HDD，减少IO争用  |
| **CPU优先**  | 2        | 30           | 32     | 高速存储         |
| **保守配置** | 4        | 12           | 16     | 低配机器         |

---

## 数据结构设计

### JSON内容队列

```cpp
struct JsonTask {
    int provinceCode;        // 省份代码
    std::string content;     // JSON文件内容（内存中）
    std::string filePath;    // 原始文件路径（用于日志）
};

ThreadSafeQueue<JsonTask> jsonTaskQueue;
```

### 队列容量控制

```cpp
// 根据内存和CPU动态调整
size_t queueSize = parseThreads * 10;  // 每线程10个任务缓冲
// 4解压 + 28处理 = 32线程, 队列 = 280
```

---

## 实现代码

### 1. 解压线程（生产者）

```cpp
void extractWorker(ThreadSafeQueue<JsonTask>& taskQueue, 
                   const ProvinceZipInfo& province) {
    // 打开ZIP文件
    unzFile zip = unzOpen(province.zipPath.c_str());
    
    unzGoToFirstFile(zip);
    do {
        char fileName[256];
        unz_file_info64 info;
        unzGetCurrentFileInfo64(zip, &info, fileName, sizeof(fileName),
                                nullptr, 0, nullptr, 0);
        
        // 只处理JSON文件
        if (!isJsonFile(fileName)) {
            continue;
        }
        
        // 解压JSON内容到内存
        unzOpenCurrentFile(zip);
        std::string jsonContent(info.uncompressed_size, '\0');
        unzReadCurrentFile(zip, jsonContent.data(), info.uncompressed_size);
        unzCloseCurrentFile(zip);
        
        // 构造任务并入队
        JsonTask task;
        task.provinceCode = province.provinceCode;
        task.content = std::move(jsonContent);
        task.filePath = province.zipPath + "/" + fileName;
        
        taskQueue.push(std::move(task));
        
    } while (unzGoToNextFile(zip) == UNZ_OK);
    
    unzClose(zip);
}
```

### 2. JSON处理线程（消费者）

```cpp
void parseWorker(ThreadSafeQueue<JsonTask>& taskQueue,
                 BlacklistChecker& checker,
                 ThreadSafeCounter& counter) {
    // 线程局部parser（复用）
    thread_local simdjson::dom::parser localParser;
    
    while (true) {
        JsonTask task = taskQueue.pop();
        
        // 收到终止信号
        if (task.content.empty() && task.provinceCode == 0) {
            break;
        }
        
        // 解析JSON
        auto jsonResult = localParser.parse(task.content);
        if (jsonResult.error()) {
            continue;
        }
        
        // 提取卡号
        std::vector<std::string> validCardIds;
        for (auto element : jsonResult.value()) {
            std::string_view cardIdView;
            if (element.get_object()["cardId"].get(cardIdView)) continue;
            
            if (isValidCardId(cardIdView)) {
                validCardIds.emplace_back(cardIdView);
            }
        }
        
        // 添加到存储
        if (!validCardIds.empty()) {
            checker.addBatch(validCardIds);
            counter.addLoaded(validCardIds.size());
        }
    }
}
```

### 3. 主函数

```cpp
void loadBlacklistOptimized(const std::vector<ProvinceZipInfo>& provinces) {
    // 计算线程配置
    size_t cpuCount = std::thread::hardware_concurrency();
    size_t extractThreads = 4;  // 解压线程（IO密集）
    size_t parseThreads = cpuCount - extractThreads;  // 处理线程（CPU密集）
    
    // 创建任务队列
    ThreadSafeQueue<JsonTask> taskQueue(parseThreads * 10);
    
    // 省份任务队列
    ThreadSafeQueue<ProvinceZipInfo> provinceQueue;
    for (const auto& p : provinces) {
        provinceQueue.push(p);
    }
    
    // 启动解压线程池
    std::vector<std::thread> extractPool;
    for (size_t i = 0; i < extractThreads; ++i) {
        extractPool.emplace_back([&]() {
            while (true) {
                auto province = provinceQueue.pop();
                if (province.provinceCode == 0) {
                    provinceQueue.push({0, ""});  // 传递终止信号
                    break;
                }
                extractWorker(taskQueue, province);
            }
        });
    }
    
    // 启动JSON处理线程池
    std::vector<std::thread> parsePool;
    ThreadSafeCounter counter;
    for (size_t i = 0; i < parseThreads; ++i) {
        parsePool.emplace_back([&]() {
            parseWorker(taskQueue, checker, counter);
        });
    }
    
    // 等待解压线程完成
    for (auto& t : extractPool) t.join();
    
    // 发送终止信号给处理线程
    for (size_t i = 0; i < parseThreads; ++i) {
        taskQueue.push({0, "", ""});  // 空任务作为终止信号
    }
    
    // 等待处理线程完成
    for (auto& t : parsePool) t.join();
    
    // 最后统一排序
    checker.sortAll();
}
```

---

## 内存管理

### 队列内存占用

```
队列容量 = parseThreads × 10 = 28 × 10 = 280个任务
每个任务 = ~150KB（JSON内容）
峰值内存 = 280 × 150KB ≈ 42MB
```

### 内存控制策略

| 策略               | 说明                      |
| ------------------ | ------------------------- |
| **队列容量限制**   | 通过 `queueSize` 参数控制 |
| **流式处理**       | 每任务处理完立即释放      |
| **线程局部parser** | 避免parser重复创建        |

---

## 优缺点分析

### 优点

| 优点            | 说明                 |
| --------------- | -------------------- |
| **IO和CPU并行** | 解压和处理流水线进行 |
| **内存可控**    | 队列容量限制内存     |
| **负载均衡**    | 动态调整线程数       |
| **充分利用CPU** | M个线程解析JSON      |
| **无磁盘写入**  | 不写磁盘，只读ZIP    |

### 缺点

| 缺点           | 说明                      |
| -------------- | ------------------------- |
| **队列内存**   | 需要存储JSON内容（~40MB） |
| **实现复杂度** | 比当前方案复杂            |
| **调参难度**   | 需要平衡解压和处理线程数  |

---

## 线程配置对比

| 配置     | 解压 | 处理 | 总线程 | 内存  |
| -------- | ---- | ---- | ------ | ----- |
| **平衡** | 4    | 28   | 32     | ~40MB |
| **保守** | 2    | 14   | 16     | ~20MB |
| **激进** | 8    | 24   | 32     | ~40MB |

---

## 结论

**这个方案结合了两种方案的优势：**

1. **多线程解压**：IO并行，减少解压等待
2. **多线程处理**：CPU并行，快速解析JSON
3. **流式处理**：内存可控，不累积
4. **流水线**：解压和处理同时进行

**推荐配置：**
- 解压线程：4个（IO密集）
- 处理线程：28个（CPU密集）
- 队列容量：280

**是否需要我实施这个方案？**