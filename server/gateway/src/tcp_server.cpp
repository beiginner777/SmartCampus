#include "tcp_server.h"

#include "logger.h"

#include <array>
#include <memory>
#include <string>
#include <utility>

namespace gateway {

using boost::asio::ip::tcp;

// ---------------------------------------------------------------------------
// Session: 单个客户端连接的生命周期
//   读数据 -> 原样回写 (echo) -> 继续读; 断线时触发 on_disconnected
//   通过 shared_from_this 让 pending 异步操作持有自身, 连接存续期间保持存活
// ---------------------------------------------------------------------------
class Session : public std::enable_shared_from_this<Session> {
public:
    Session(boost::asio::io_context& io, TcpServer& server) : socket_(io), server_(server) {}

    ~Session() { socket_.close(); }

    tcp::socket& socket() { return socket_; }

    void start() {
        // 保存客户端的地址
        remote_ = socket_.remote_endpoint();
        do_read();
    }

private:
    static constexpr size_t kBufferSize = 4096;

    void do_read() {
        auto self = shared_from_this();
        socket_.async_read_some(
            boost::asio::buffer(buffer_),
            // detail:捕获self，防止在回调函数触发之前，this指针指向的Session被析构了，出现悬空指针
            [this, self](boost::system::error_code ec, std::size_t n) {
                if (ec) {
                    on_error(ec);
                    return;
                }
                std::string data(buffer_.data(), n);
                if (server_.on_received) {
                    server_.on_received(remote_string(), data);
                }
                do_write(std::move(data));  // echo 回显
            });
    }

    void do_write(std::string data) {
        auto self = shared_from_this();
        boost::asio::async_write(
            socket_, boost::asio::buffer(data),
            [this, self](boost::system::error_code ec, std::size_t /*n*/) {
                if (ec) {
                    on_error(ec);
                    return;
                }
                do_read();
            });
    }

    void on_error(const boost::system::error_code& ec) {
        if (server_.on_disconnected) {
            server_.on_disconnected(remote_string(), ec.message());
        }
        // 异步循环结束后, socket 随 Session 析构自动关闭
    }

    std::string remote_string() const {
        return remote_.address().to_string() + ":" + std::to_string(remote_.port());
    }

    tcp::socket socket_;
    TcpServer& server_;
    tcp::endpoint remote_;
    std::array<char, kBufferSize> buffer_{};
};

TcpServer::TcpServer(boost::asio::io_context& io, Options opt)
    : io_(io),
      opt_(std::move(opt)),
      acceptor_(io, tcp::endpoint(boost::asio::ip::make_address(opt_.host), opt_.port)) {
    acceptor_.set_option(tcp::acceptor::reuse_address(true));
}

TcpServer::~TcpServer() = default;

void TcpServer::start() {
    do_accept();
    LOG_INFO("TCP 服务监听 {}:{}", opt_.host, opt_.port);
}

void TcpServer::stop() {
    boost::system::error_code ec;
    acceptor_.close(ec);
}

void TcpServer::do_accept() {
    auto session = std::make_shared<Session>(io_, *this);
    acceptor_.async_accept(session->socket(), [this, session](const boost::system::error_code& ec) {
        if (ec) {
            // 业务正常执行可能抛出的错误（socket被关闭、io_context被终止等）
            if (ec != boost::asio::error::operation_aborted) {
                LOG_ERROR("accept 失败: {}", ec.message());
            }
            return;
        }
        session->start();
        if (on_connected) {
            const auto ep = session->socket().remote_endpoint();
            on_connected(ep.address().to_string() + ":" + std::to_string(ep.port()));
        }
        do_accept();
    });
}

}  // namespace gateway
