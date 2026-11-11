#include "http_server.h"

#include <json/json.h>

#include <memory>
#include <string>
#include <utility>

#include "logger.h"

namespace auth {

namespace {

std::string json_to_string(const Json::Value& v) {
    Json::FastWriter w;
    std::string s = w.write(v);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {
        s.pop_back();
    }
    return s;
}

http::response<http::string_body> json_response(const Json::Value& v, unsigned version) {
    http::response<http::string_body> resp;
    resp.version(version);
    resp.result(http::status::ok);
    resp.set(http::field::server, "smart-campus-auth");
    resp.set(http::field::content_type, "application/json");
    resp.body() = json_to_string(v);
    resp.prepare_payload();
    return resp;
}

}  // namespace

HttpSession::HttpSession(tcp::socket socket, AppState& st) : socket_(std::move(socket)), st_(st) {}

void HttpSession::run() { read_request(); }

void HttpSession::read_request() {
    auto self = shared_from_this();
    http::async_read(socket_, buffer_, req_, [self](beast::error_code ec, std::size_t /*n*/) {
        if (ec == http::error::end_of_stream) {
            self->close();
            return;
        }
        if (ec) {
            return;
        }
        self->handle_request();
    });
}

void HttpSession::handle_request() {
    // response 用 shared_ptr 持有：http::async_write 引用消息体，需存活到写完成
    auto resp = std::make_shared<http::response<http::string_body>>(dispatch(req_, st_));
    auto self = shared_from_this();
    http::async_write(socket_, *resp, [self, resp](beast::error_code ec, std::size_t /*n*/) {
        if (!ec) {
            self->close();
        }
    });
}

void HttpSession::close() {
    beast::error_code ec;
    socket_.shutdown(tcp::socket::shutdown_send, ec);
}

http::response<http::string_body> HttpSession::dispatch(const http::request<http::string_body>& req,
                                                        AppState& st) {
    Json::Value body;
    if (!req.body().empty()) {
        Json::Reader reader;
        if (!reader.parse(req.body(), body)) {
            Json::Value err;
            err["code"] = 1;
            err["msg"] = "参数解析失败";
            return json_response(err, req.version());
        }
    }

    Json::Value resp;
    if (req.method() == http::verb::post && req.target() == "/register") {
        resp = handle_register(body, st);
    } else if (req.method() == http::verb::post && req.target() == "/login") {
        resp = handle_login(body, st);
    } else if (req.method() == http::verb::post && req.target() == "/refresh") {
        resp = handle_refresh(body, st);
    } else {
        resp["code"] = 6;
        resp["msg"] = "未知接口";
    }
    return json_response(resp, req.version());
}

HttpServer::HttpServer(net::io_context& io, const tcp::endpoint& ep, AppState& st)
    : acceptor_(io, ep), st_(st) {
    acceptor_.set_option(net::socket_base::reuse_address(true));
    do_accept();
}

void HttpServer::do_accept() {
    acceptor_.async_accept([this](beast::error_code ec, tcp::socket socket) {
        if (!ec) {
            LOG_INFO("accepted new connection: {}", socket.remote_endpoint().address().to_string());
            std::make_shared<HttpSession>(std::move(socket), st_)->run();
        } else {
            LOG_ERROR("acceptor accept error: {}", ec.message());
        }
        do_accept();
    });
}

}  // namespace auth
