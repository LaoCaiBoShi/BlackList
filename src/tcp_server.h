#pragma once

#include <string>
#include <functional>

/**
 * @brief TCP 查询服务器类
 * @details 使用 coost 库实现高性能 TCP 服务器
 */
class TcpQueryServer {
public:
    /**
     * @brief 构造函数
     * @param port 监听端口
     */
    TcpQueryServer(int port);

    /**
     * @brief 析构函数
     */
    ~TcpQueryServer();

    /**
     * @brief 设置查询回调函数
     * @param callback 查询回调函数，接收卡号字符串，返回查询结果
     */
    void setQueryCallback(std::function<std::string(const std::string&)> callback);

    /**
     * @brief 启动 TCP 服务器
     */
    void start();

    /**
     * @brief 停止 TCP 服务器
     */
    void stop();

    /**
     * @brief 检查服务器是否正在运行
     * @return 是否运行
     */
    bool isRunning() const { return m_running; }

    /**
     * @brief 获取监听端口
     * @return 端口号
     */
    int getPort() const { return m_port; }

private:
    int m_port;
    bool m_running;
    std::function<std::string(const std::string&)> m_queryCallback;
    void* m_server;
};

/**
 * @brief 创建 TCP 查询服务器
 * @param port 监听端口
 * @param queryCallback 查询回调函数
 * @return TCP 查询服务器实例
 */
TcpQueryServer* createTcpQueryServer(int port, std::function<std::string(const std::string&)> queryCallback);

/**
 * @brief 销毁 TCP 查询服务器
 * @param server TCP 查询服务器实例
 */
void destroyTcpQueryServer(TcpQueryServer* server);
