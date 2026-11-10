#pragma once

#include "config.h"
#include "jwt_auth.h"
#include "message_router.h"
#include "pg_pool.h"
#include "redis_pool.h"
#include "session.h"

#include <boost/asio.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace gateway {

// 鉴权与连接管理：注册/登录/登出/心跳 + 异地互踢 + uid→连接表 + 心跳超时检测
class AuthService {
public:
    using SessionPtr = std::shared_ptr<Session>;

    AuthService(boost::asio::io_context& io,
                common::PgPool& pg,
                common::RedisPool& redis,
                common::JwtCfg jwt_cfg,
                common::HeartbeatCfg hb);

    // 启动心跳检测定时器
    void start();

    void handle_packet(SessionPtr session, const common::Packet& pkt);
    void handle_disconnect(SessionPtr session, const std::string& reason);

private:
    void handle_register(const SessionPtr& session, uint32_t seq, const std::string& body);
    void handle_login(const SessionPtr& session, uint32_t seq, const std::string& body);
    void handle_logout(const SessionPtr& session, uint32_t seq, const std::string& body);
    void handle_heartbeat(const SessionPtr& session, uint32_t seq, const std::string& body);

    void kick(const SessionPtr& session, const std::string& reason);
    void check_heartbeats();
    void schedule_heartbeat();
    void send_error(const SessionPtr& session, uint32_t seq, int code, const std::string& msg);

    boost::asio::io_context& io_;
    common::PgPool& pg_;
    common::RedisPool& redis_;
    common::JwtAuth jwt_;
    int jwt_expire_seconds_ = 7200;
    common::HeartbeatCfg hb_;
    common::MessageRouter<SessionPtr> router_;

    std::mutex mutex_;
    std::unordered_map<uint32_t, std::weak_ptr<Session>> sessions_;  // uid → 连接
};

}  // namespace gateway
