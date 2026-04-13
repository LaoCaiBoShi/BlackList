#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>

/**
 * @brief 线程池类，用于管理线程和任务
 */
class ThreadPool {
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable cv;
    bool stop;

public:
    /**
     * @brief 构造函数
     * @param threads 线程数量
     */
    ThreadPool(size_t threads);

    /**
     * @brief 析构函数
     */
    ~ThreadPool();

    /**
     * @brief 添加任务到线程池
     * @tparam F 任务函数类型
     * @param f 任务函数
     * @return 任务的future对象
     */
    template<class F>
    std::future<void> enqueue(F&& f);
};

// 模板函数实现
template<class F>
std::future<void> ThreadPool::enqueue(F&& f) {
    auto task = std::make_shared<std::packaged_task<void()>>(std::forward<F>(f));
    std::future<void> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        tasks.emplace([task]() { (*task)(); });
    }
    cv.notify_one();
    return res;
}

#endif // THREAD_POOL_H
