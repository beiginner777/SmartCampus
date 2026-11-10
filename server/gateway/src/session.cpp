#include "session.h"

#include "logger.h"

#include <utility>

namespace gateway {

Session::Session(boost::asio::io_context& io) : socket_(io) {}

void Session::start() {
    auto ep = socket_.remote_endpoint();
    remote_ = ep.address().to_string() + ":" + std::to_string(ep.port());
    touch_heartbeat();
    do_read();
}

void Session::stop() {
    boost::system::error_code ec;
    socket_.close(ec);
}

void Session::set_auth(uint32_t uid, std::string username) {
    uid_ = uid;
    username_ = std::move(username);
}

void Session::touch_heartbeat() {
    last_heartbeat_ = std::chrono::steady_clock::now();
}

void Session::do_read() {
    auto self = shared_from_this();
    socket_.async_read_some(boost::asio::buffer(buffer_),
                            [this, self](boost::system::error_code ec, std::size_t n) {
                                if (ec) {
                                    if (on_disconnect) {
                                        on_disconnect(self, ec.message());
                                    }
                                    return;
                                }
                                auto packets = decoder_.feed(buffer_.data(), n);
                                if (decoder_.has_error()) {
                                    LOG_WARN("协议错误，断开连接 [{}]", remote_);
                                    stop();
                                    return;
                                }
                                for (auto& pkt : packets) {
                                    if (on_packet) {
                                        on_packet(self, pkt);
                                    }
                                }
                                do_read();
                            });
}

void Session::send_packet(uint16_t msg_type, uint32_t seq, const std::string& body) {
    auto bytes = common::encode_packet(msg_type, seq, body);
    bool start_now = false;
    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        write_queue_.push_back(std::move(bytes));
        if (!writing_) {
            writing_ = true;
            start_now = true;
        }
    }
    if (start_now) {
        boost::asio::post(socket_.get_executor(),
                          [self = shared_from_this()] { self->do_write(); });
    }
}

void Session::do_write() {
    std::shared_ptr<std::string> data;
    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        if (write_queue_.empty()) {
            writing_ = false;
            return;
        }
        // 用 shared_ptr 持有：async_write 引用缓冲内存，需存活到写完成
        data = std::make_shared<std::string>(std::move(write_queue_.front()));
        write_queue_.erase(write_queue_.begin());
    }
    auto self = shared_from_this();
    boost::asio::async_write(socket_, boost::asio::buffer(*data),
                             [self, data](boost::system::error_code ec, std::size_t /*n*/) {
                                 if (ec) {
                                     return;  // 写失败（对端关闭），交给读循环处理
                                 }
                                 self->do_write();
                             });
}

}  // namespace gateway
