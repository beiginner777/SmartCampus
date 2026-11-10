#include "tcp_server.h"

#include "logger.h"

#include <memory>
#include <utility>

namespace gateway {

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
    auto session = std::make_shared<Session>(io_);
    acceptor_.async_accept(session->socket(), [this, session](const boost::system::error_code& ec) {
        if (ec) {
            if (ec != boost::asio::error::operation_aborted) {
                LOG_ERROR("accept 失败: {}", ec.message());
            }
            return;
        }
        // 回调只捕获 this（TcpServer），session 由 shared_ptr 参数传入，避免循环引用
        session->on_packet = [this](SessionPtr s, const common::Packet& pkt) {
            if (on_packet) {
                on_packet(s, pkt);
            }
        };
        session->on_disconnect = [this](SessionPtr s, const std::string& reason) {
            if (on_disconnected) {
                on_disconnected(s, reason);
            }
        };
        session->start();
        if (on_connected) {
            on_connected(session);
        }
        do_accept();
    });
}

}  // namespace gateway
