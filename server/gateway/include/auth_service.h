#pragma once

#include "config.h"
#include "jwt_auth.h"
#include "message_router.h"
#include "redis_pool.h"
#include "session.h"

#include <cstdint>
#include <memory>
#include <string>

namespace gateway {

class TcpServer;

// 网关侧鉴权：JWT 无状态校验（验签名 + token 新鲜度）+ 登出 + 心跳响应。
// 注册/登录/签发 token 已下沉到独立的 AuthServer(HTTP)，网关只验 JWT。
// 连接表（sessions_）、心跳超时检测、互踢均归 TcpServer 管理，本类不再持有连接。
class AuthService {
public:
    using SessionPtr = std::shared_ptr<Session>;

    AuthService(TcpServer& tcp, common::RedisPool& redis, common::JwtCfg jwt_cfg);

    void handle_packet(SessionPtr session, const common::Packet& pkt);

private:
    void handle_auth(const SessionPtr& session, uint32_t seq, const std::string& body);
    void handle_logout(const SessionPtr& session, uint32_t seq, const std::string& body);
    void handle_heartbeat(const SessionPtr& session, uint32_t seq, const std::string& body);
    void send_error(const SessionPtr& session, uint32_t seq, int code, const std::string& msg);

    TcpServer& tcp_;
    common::RedisPool& redis_;
    common::JwtAuth jwt_;
    common::MessageRouter<SessionPtr> router_;
};

}  // namespace gateway
