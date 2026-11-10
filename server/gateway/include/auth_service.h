#pragma once

#include "config.h"
#include "jwt_auth.h"
#include "message_router.h"
#include "redis_pool.h"
#include "session.h"

#include <boost/asio.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace gateway {

// 网关侧连接管理：JWT 无状态鉴权 + 心跳保活 + 异地互踢（gRPC 控制面）
// 注册/登录/签发 token 已下沉到独立的 AuthServer(HTTP)，网关只验 JWT 签名。
class AuthService {
public:
    using SessionPtr = std::shared_ptr<Session>;

    AuthService(boost::asio::io_context& io,
                common::RedisPool& redis,
                common::JwtCfg jwt_cfg,
                common::HeartbeatCfg hb,
                const std::string& gateway_grpc_addr);

    // 启动心跳检测定时器
    void start();

    void handle_packet(SessionPtr session, const common::Packet& pkt);
    void handle_disconnect(SessionPtr session, const std::string& reason);

    // 踢掉 uid 的旧连接（供 KickService 的 gRPC 调用）
    void kick_uid(uint32_t uid, const std::string& reason);

private:
    void handle_auth(const SessionPtr& session, uint32_t seq, const std::string& body);
    void handle_logout(const SessionPtr& session, uint32_t seq, const std::string& body);
    void handle_heartbeat(const SessionPtr& session, uint32_t seq, const std::string& body);

    void kick(const SessionPtr& session, const std::string& reason);
    void check_heartbeats();
    void schedule_heartbeat();
    void send_error(const SessionPtr& session, uint32_t seq, int code, const std::string& msg);

    boost::asio::io_context& io_;
    common::RedisPool& redis_;
    common::JwtAuth jwt_;
    common::HeartbeatCfg hb_;
    std::string gateway_grpc_addr_;  // 本网关 gRPC 地址，鉴权成功后写进路由表
    common::MessageRouter<SessionPtr> router_;

    std::mutex mutex_;
    std::unordered_map<uint32_t, std::weak_ptr<Session>> sessions_;  // uid → 连接
};

}  // namespace gateway
