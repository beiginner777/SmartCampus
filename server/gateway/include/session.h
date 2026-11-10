#pragma once

#include "codec.h"

#include <boost/asio.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace gateway {

using tcp = boost::asio::ip::tcp;

// 单个客户端连接：协议编解码（粘包/拆包）、收发、鉴权态与心跳
class Session : public std::enable_shared_from_this<Session> {
public:
    explicit Session(boost::asio::io_context& io);

    tcp::socket& socket() { return socket_; }

    void start();
    void stop();  // 立即关闭连接

    // 发送一帧（线程安全，供心跳/互踢等跨线程调用）
    void send_packet(uint16_t msg_type, uint32_t seq, const std::string& body);

    // 鉴权态
    bool authenticated() const { return uid_ != 0; }
    uint32_t uid() const { return uid_; }
    const std::string& username() const { return username_; }
    void set_auth(uint32_t uid, std::string username);

    // 心跳
    void touch_heartbeat();
    std::chrono::steady_clock::time_point last_heartbeat() const { return last_heartbeat_; }

    const std::string& remote() const { return remote_; }

    // 收到完整包 / 连接断开 的回调（由上层设置，session 以 shared_ptr 传入）
    std::function<void(std::shared_ptr<Session>, const common::Packet&)> on_packet;
    std::function<void(std::shared_ptr<Session>, const std::string& reason)> on_disconnect;

private:
    void do_read();
    void do_write();

    tcp::socket socket_;
    common::PacketDecoder decoder_;
    std::array<char, 4096> buffer_{};
    std::string remote_;

    std::mutex write_mutex_;
    std::vector<std::string> write_queue_;
    bool writing_ = false;

    uint32_t uid_ = 0;
    std::string username_;
    std::chrono::steady_clock::time_point last_heartbeat_;
};

}  // namespace gateway
