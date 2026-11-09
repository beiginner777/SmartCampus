#pragma once

#include <boost/asio.hpp>

#include <cstdint>
#include <functional>
#include <string>

namespace gateway {

// 最小 TCP 服务器 (M0: EchoServer)
//   - 接受连接 / 收发数据 / 断线回调
//   - 基于 boost::asio 封装的异步IO
class TcpServer {
public:
    struct Options {
        std::string host = "0.0.0.0";  // 监听地址 (IP 字面量)
        uint16_t port = 9000;
    };

    // 回调：连接建立 / 收到数据 / 连接断开 (均在 io_context 线程内触发)
    std::function<void(const std::string& remote_endpoint)> on_connected;
    std::function<void(const std::string& remote_endpoint, const std::string& data)> on_received;
    std::function<void(const std::string& remote_endpoint, const std::string& reason)>
        on_disconnected;

    TcpServer(boost::asio::io_context& io, Options opt);
    ~TcpServer();

    void start();
    void stop();

private:
    void do_accept();

    boost::asio::io_context& io_;
    Options opt_;
    boost::asio::ip::tcp::acceptor acceptor_;
};

}  // namespace gateway
