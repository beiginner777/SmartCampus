#pragma once

#include "auth_handler.h"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <memory>

namespace auth {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

// HTTP 会话：读一个请求 → 路由 → 写响应 → 关闭（M1 单请求/连接）
class HttpSession : public std::enable_shared_from_this<HttpSession> {
public:
    HttpSession(tcp::socket socket, AppState& st);

    void run();

private:
    void read_request();
    void handle_request();
    void close();

    static http::response<http::string_body> dispatch(const http::request<http::string_body>& req,
                                                      AppState& st);

    tcp::socket socket_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> req_;
    AppState& st_;
};

// HTTP 监听器：接受连接，为每个连接创建 HttpSession
class HttpServer {
public:
    HttpServer(net::io_context& io, const tcp::endpoint& ep, AppState& st);

private:
    void do_accept();

    tcp::acceptor acceptor_;
    AppState& st_;
};

}  // namespace auth
