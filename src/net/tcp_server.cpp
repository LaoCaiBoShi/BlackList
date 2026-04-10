#define _CRT_SECURE_NO_WARNINGS

#include "tcp_server.h"
#include "blacklist_checker.h"
#include <iostream>
#include <thread>
#include <co/all.h>

/**
 * @brief TCP 查询服务器实现类
 */
class TcpQueryServerImpl {
public:
    TcpQueryServerImpl(int port) : m_port(port), m_running(false), m_server(nullptr) {}

    ~TcpQueryServerImpl() {
        stop();
    }

    void setQueryCallback(std::function<std::string(const std::string&)> callback) {
        m_queryCallback = callback;
    }

    void start() {
        if (m_running) {
            std::cout << "TCP Server already running on port " << m_port << std::endl;
            return;
        }

        m_running = true;

        // 在协程中启动服务器
        co::go([this]() {
            tcp::Server server;

            server.on_connection([this](tcp::Connection conn) {
                // 接收数据
                char buf[1024];
                int n = conn.recv(buf, sizeof(buf) - 1);

                if (n > 0) {
                    buf[n] = '\0';
                    std::string cardId(buf);

                    // 去除空白字符
                    cardId.erase(0, cardId.find_first_not_of(" \t\n\r"));
                    size_t lastPos = cardId.find_last_not_of(" \t\n\r");
                    if (lastPos != std::string::npos) {
                        cardId.erase(lastPos + 1);
                    }

                    std::cout << "TCP Received query: " << cardId << std::endl;

                    // 查询黑名单
                    std::string response;
                    if (m_queryCallback) {
                        response = m_queryCallback(cardId);
                    } else {
                        response = "ERROR: No callback set\n";
                    }

                    conn.send(response.c_str(), response.size());
                    std::cout << "TCP Sent response: " << response.substr(0, response.size() - 1) << std::endl;
                }

                conn.close();
            });

            std::cout << "TCP Query Server starting on port " << m_port << std::endl;
            server.start(nullptr, m_port);
            std::cout << "TCP Query Server started on port " << m_port << std::endl;

            // 保持协程运行
            while (m_running) {
                co::sleep(1000);
            }
        });
    }

    void stop() {
        if (!m_running) {
            return;
        }

        m_running = false;
        std::cout << "TCP Query Server stopped" << std::endl;
    }

    bool isRunning() const {
        return m_running;
    }

    int getPort() const {
        return m_port;
    }

private:
    int m_port;
    bool m_running;
    std::function<std::string(const std::string&)> m_queryCallback;
    tcp::Server* m_server;
};

/**
 * @brief TCP 查询服务器构造函数
 */
TcpQueryServer::TcpQueryServer(int port)
    : m_port(port), m_running(false), m_server(nullptr) {
}

/**
 * @brief TCP 查询服务器析构函数
 */
TcpQueryServer::~TcpQueryServer() {
    stop();
}

/**
 * @brief 设置查询回调函数
 */
void TcpQueryServer::setQueryCallback(std::function<std::string(const std::string&)> callback) {
    m_queryCallback = callback;
}

/**
 * @brief 启动 TCP 服务器
 */
void TcpQueryServer::start() {
    if (m_running) {
        std::cout << "TCP Server already running on port " << m_port << std::endl;
        return;
    }

    m_running = true;

    // 在新线程中启动协程服务器
    std::thread([this]() {
        tcp::Server server;

        server.on_connection([this](tcp::Connection conn) {
            // 接收数据
            char buf[1024];
            int n = conn.recv(buf, sizeof(buf) - 1);

            if (n > 0) {
                buf[n] = '\0';
                std::string cardId(buf);

                // 去除空白字符
                cardId.erase(0, cardId.find_first_not_of(" \t\n\r"));
                size_t lastPos = cardId.find_last_not_of(" \t\n\r");
                if (lastPos != std::string::npos) {
                    cardId.erase(lastPos + 1);
                }

                std::cout << "TCP Received query: " << cardId << std::endl;

                // 查询黑名单
                std::string response;
                if (m_queryCallback) {
                    response = m_queryCallback(cardId);
                } else {
                    response = "ERROR: No callback set\n";
                }

                conn.send(response.c_str(), response.size());
                std::cout << "TCP Sent response: " << response.substr(0, response.size() - 1) << std::endl;
            }

            conn.close();
        });

        std::cout << "TCP Query Server started on port " << m_port << std::endl;
        server.start(nullptr, m_port);

        // 保持运行
        while (m_running) {
            co::sleep(100);
        }
    }).detach();
}

/**
 * @brief 停止 TCP 服务器
 */
void TcpQueryServer::stop() {
    if (!m_running) {
        return;
    }

    m_running = false;
    std::cout << "TCP Query Server stopped" << std::endl;
}

/**
 * @brief 创建 TCP 查询服务器
 */
TcpQueryServer* createTcpQueryServer(int port, std::function<std::string(const std::string&)> queryCallback) {
    TcpQueryServer* server = new TcpQueryServer(port);
    server->setQueryCallback(queryCallback);
    return server;
}

/**
 * @brief 销毁 TCP 查询服务器
 */
void destroyTcpQueryServer(TcpQueryServer* server) {
    if (server) {
        delete server;
    }
}
