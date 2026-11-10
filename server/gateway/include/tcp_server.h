#pragma once

#include "session.h"

#include <boost/asio.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace gateway {

// TCP 接入服务器：接受连接，为每个连接创建 Session（基于 boost::asio 异步 IO）
class TcpServer {
public:
    struct Options {
        std::string host = "0.0.0.0";  // 监听地址 (IP 字面量)
        uint16_t port = 9000;
    };

    using SessionPtr = std::shared_ptr<Session>;

    // 回调：连接建立 / 收到完整包 / 连接断开
    std::function<void(const SessionPtr&)> on_connected;
    std::function<void(const SessionPtr&, const common::Packet&)> on_packet;
    std::function<void(const SessionPtr&, const std::string&)> on_disconnected;

    TcpServer(boost::asio::io_context& io, Options opt);
    ~TcpServer();

    void start();
    void stop();

private:
    void do_accept();

    boost::asio::io_context& io_;
    Options opt_;
    tcp::acceptor acceptor_;
};

}  // namespace gateway
