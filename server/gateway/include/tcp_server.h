#pragma once

#include "config.h"
#include "redis_pool.h"
#include "session.h"

#include <boost/asio.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace gateway {

// TCP 接入服务器 + 连接管理器：
//   - accept 连接，为每个连接创建 Session
//   - 维护 uid → Session 连接表（sessions_）
//   - 心跳保活（定时检测超时踢下线）
//   - 异地互踢（kick_uid，供 KickService 的 gRPC 调用）
//   - 路由表 conn:{uid}（建连写、心跳刷新、断连删）
class TcpServer {
public:
    struct Options {
        std::string host = "0.0.0.0";  // 监听地址 (IP 字面量)
        uint16_t port = 9000;
    };

    using SessionPtr = std::shared_ptr<Session>;

    TcpServer(boost::asio::io_context& io, Options opt, common::RedisPool& redis,
              common::HeartbeatCfg hb, const std::string& gateway_grpc_addr);
    ~TcpServer();

    // 启动：accept + 心跳定时器
    void start();
    void stop();

    // 连接管理
    void register_session(uint32_t uid, SessionPtr session);    // 鉴权成功后注册 + 写 conn:{uid}
    void unregister_session(uint32_t uid, SessionPtr session);  // 断开/登出清理 + 删 conn:{uid}
    // 会话下线（logout 主动 / 断开异常）：清连接表 + 删 conn + 通知业务层（AOI 清理）
    void close_session(uint32_t uid, SessionPtr session, const std::string& reason);
    SessionPtr find_session(uint32_t uid);                      // 查在线连接（无则空）
    void send_to(uint32_t uid, uint16_t msg_type, const std::string& body);
    void refresh_conn(uint32_t uid);                            // 心跳刷新 conn:{uid}
    void kick_uid(uint32_t uid, const std::string& reason);     // 异地互踢

    // 回调：连接建立 / 收到完整包 / 连接断开（断开时已先清理连接表）
    std::function<void(const SessionPtr&)> on_connected;
    std::function<void(const SessionPtr&, const common::Packet&)> on_packet;
    std::function<void(const SessionPtr&, const std::string&)> on_disconnected;

private:
    void do_accept();
    void kick(const SessionPtr& session, const std::string& reason);
    void check_heartbeats();
    void schedule_heartbeat();

    boost::asio::io_context& io_;
    Options opt_;
    tcp::acceptor acceptor_;
    common::RedisPool& redis_;
    common::HeartbeatCfg hb_;
    std::string gateway_grpc_addr_;  // 本网关 gRPC 地址，写入路由表

    std::mutex mutex_;
    std::unordered_map<uint32_t, std::weak_ptr<Session>> sessions_;  // uid → 连接
};

}  // namespace gateway
