#include "auth_service.h"

#include "logger.h"
#include "tcp_server.h"

#include <json/json.h>

#include <string>

namespace gateway {

namespace {

std::string json_to_string(const Json::Value& v) {
    Json::FastWriter w;
    std::string s = w.write(v);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {
        s.pop_back();
    }
    return s;
}

void send_json(const AuthService::SessionPtr& s, uint16_t msg_type, uint32_t seq,
               const Json::Value& v) {
    s->send_packet(msg_type, seq, json_to_string(v));
}

}  // namespace

AuthService::AuthService(TcpServer& tcp, common::RedisPool& redis, common::JwtCfg jwt_cfg)
    : tcp_(tcp), redis_(redis), jwt_(jwt_cfg.secret) {
    using common::MsgType;
    router_.register_handler(static_cast<uint16_t>(MsgType::kAuthReq),
                             [this](SessionPtr s, uint32_t seq, const std::string& b) {
                                 handle_auth(s, seq, b);
                             });
    router_.register_handler(static_cast<uint16_t>(MsgType::kLogoutReq),
                             [this](SessionPtr s, uint32_t seq, const std::string& b) {
                                 handle_logout(s, seq, b);
                             });
    router_.register_handler(static_cast<uint16_t>(MsgType::kHeartbeatReq),
                             [this](SessionPtr s, uint32_t seq, const std::string& b) {
                                 handle_heartbeat(s, seq, b);
                             });
}

void AuthService::handle_packet(SessionPtr session, const common::Packet& pkt) {
    if (!router_.dispatch(pkt.msg_type, session, pkt.seq, pkt.body)) {
        LOG_WARN("未注册消息类型 0x{:04x} [{}]", pkt.msg_type, session->remote());
        send_error(session, pkt.seq, 6, "未知消息类型");
    }
}

void AuthService::handle_auth(const SessionPtr& session, uint32_t seq,
                              const std::string& body) {
    Json::Value req;
    Json::Reader reader;
    if (!reader.parse(body, req)) {
        send_error(session, seq, 1, "参数解析失败");
        return;
    }
    std::string token = req.isMember("token") ? req["token"].asString() : "";

    common::JwtPayload payload;
    if (!jwt_.verify(token, payload)) {
        send_error(session, seq, 6, "无效或过期的 token");
        return;
    }

    // 校验 token 新鲜度（堵住异地互踢的竞态）：
    //   异地互踢的"快路径"依赖路由表 conn:{uid}，而它只在建连时才写。若"B 登录"发生在"A 建连"之前，
    //   AuthServer 登录时查 conn:{uid} 为空 → 漏踢；A 随后拿旧 token 建连，网关只验 JWT 签名（仍有效）
    //   仍会放行，导致 A、B 同时在线。
    //   这里再查 Redis session:{uid}（每次登录都会覆盖成最新 token）：若所带 token 已不是当前 session，
    //   说明它被更新的登录取代了 → 当场拒绝，堵死竞态。
    auto current = redis_.get("session:" + std::to_string(payload.uid));
    if (current && *current != token) {
        LOG_INFO("拒绝过时 token 建连：uid={}", payload.uid);
        send_error(session, seq, 6, "token 已失效，请重新登录");
        return;
    }

    session->set_auth(payload.uid, payload.username, payload.nickname);
    session->touch_heartbeat();
    // 注册连接 + 写路由表 conn:{uid}（归 TcpServer 管）
    tcp_.register_session(payload.uid, session);

    Json::Value resp;
    resp["code"] = 0;
    resp["uid"] = payload.uid;
    resp["username"] = payload.username;
    send_json(session, static_cast<uint16_t>(common::MsgType::kAuthResp), seq, resp);
    LOG_INFO("连接鉴权成功: {} (uid={})", payload.username, payload.uid);
}

void AuthService::handle_logout(const SessionPtr& session, uint32_t seq,
                                const std::string& /*body*/) {
    if (!session->authenticated()) {
        send_error(session, seq, 4, "未登录");
        return;
    }
    uint32_t uid = session->uid();
    // 主动下线：作废会话 token
    redis_.del("session:" + std::to_string(uid));
    // 统一下线清理：清连接表 + 删 conn + 通知 AOI 清理（广播 USER_LEAVE）
    tcp_.close_session(uid, session, "logout");
    session->set_auth(0, "", "");
    Json::Value resp;
    resp["code"] = 0;
    send_json(session, static_cast<uint16_t>(common::MsgType::kLogoutResp), seq, resp);
}

void AuthService::handle_heartbeat(const SessionPtr& session, uint32_t seq,
                                   const std::string& /*body*/) {
    if (!session->authenticated()) {
        send_error(session, seq, 4, "未登录");
        return;
    }
    session->touch_heartbeat();
    // 刷新路由表 conn:{uid} 续期（归 TcpServer 管）
    tcp_.refresh_conn(session->uid());
    Json::Value resp;
    resp["code"] = 0;
    send_json(session, static_cast<uint16_t>(common::MsgType::kHeartbeatResp), seq, resp);
}

void AuthService::send_error(const SessionPtr& session, uint32_t seq, int code,
                             const std::string& msg) {
    Json::Value v;
    v["code"] = code;
    v["msg"] = msg;
    send_json(session, static_cast<uint16_t>(common::MsgType::kError), seq, v);
}

}  // namespace gateway
