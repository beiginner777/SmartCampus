#include "auth_service.h"

#include "logger.h"

#include <json/json.h>

#include <chrono>
#include <cstdlib>
#include <memory>
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

AuthService::AuthService(boost::asio::io_context& io, common::RedisPool& redis,
                         common::JwtCfg jwt_cfg, common::HeartbeatCfg hb)
    : io_(io), redis_(redis), jwt_(jwt_cfg.secret), hb_(hb) {
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

void AuthService::start() {
    schedule_heartbeat();
    // 订阅 Redis kick 频道：AuthServer 登录时广播，网关据此踢旧连接（异地互踢）
    redis_.subscribe("kick", [this](const std::string& msg) {
        uint32_t uid = static_cast<uint32_t>(std::strtoul(msg.c_str(), nullptr, 10));
        LOG_INFO("收到互踢通知 uid={}", uid);
        kick_uid(uid);
    });
}

void AuthService::handle_packet(SessionPtr session, const common::Packet& pkt) {
    if (!router_.dispatch(pkt.msg_type, session, pkt.seq, pkt.body)) {
        LOG_WARN("未注册消息类型 0x{:04x} [{}]", pkt.msg_type, session->remote());
        send_error(session, pkt.seq, 6, "未知消息类型");
    }
}

void AuthService::handle_disconnect(SessionPtr session, const std::string& reason) {
    if (!session->authenticated()) {
        return;
    }
    uint32_t uid = session->uid();
    LOG_INFO("用户 {} (uid={}) 断开: {}", session->username(), uid, reason);
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(uid);
    if (it != sessions_.end()) {
        auto s = it->second.lock();
        if (!s || s.get() == session.get()) {
            sessions_.erase(it);
        }
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

    session->set_auth(payload.uid, payload.username);
    session->touch_heartbeat();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_[payload.uid] = session;
    }

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
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(uid);
        if (it != sessions_.end()) {
            auto s = it->second.lock();
            if (!s || s.get() == session.get()) {
                sessions_.erase(it);
            }
        }
    }
    session->set_auth(0, "");
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
    Json::Value resp;
    resp["code"] = 0;
    send_json(session, static_cast<uint16_t>(common::MsgType::kHeartbeatResp), seq, resp);
}

void AuthService::kick(const SessionPtr& session, const std::string& reason) {
    Json::Value v;
    v["reason"] = reason;
    session->send_packet(static_cast<uint16_t>(common::MsgType::kKickNotify), 0,
                         json_to_string(v));
    // 短暂延迟后关闭，确保 KICK 通知送达
    auto timer =
        std::make_shared<boost::asio::steady_timer>(io_, std::chrono::milliseconds(200));
    timer->async_wait([session](const boost::system::error_code& ec) {
        if (!ec) {
            session->stop();
        }
    });
}

void AuthService::kick_uid(uint32_t uid) {
    std::shared_ptr<Session> target;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(uid);
        if (it != sessions_.end()) {
            target = it->second.lock();
            sessions_.erase(it);
        }
    }
    if (target) {
        kick(target, "已在别处登录");
    }
}

void AuthService::check_heartbeats() {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        auto s = it->second.lock();
        if (!s) {
            it = sessions_.erase(it);
            continue;
        }
        auto elapsed =
            std::chrono::duration_cast<std::chrono::seconds>(now - s->last_heartbeat()).count();
        if (elapsed > hb_.timeout_seconds) {
            LOG_INFO("用户 {} (uid={}) 心跳超时，踢下线", s->username(), s->uid());
            kick(s, "心跳超时");
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

void AuthService::schedule_heartbeat() {
    if (hb_.interval_seconds <= 0) {
        return;
    }
    auto timer =
        std::make_shared<boost::asio::steady_timer>(io_, std::chrono::seconds(hb_.interval_seconds));
    timer->async_wait([this, timer](const boost::system::error_code& ec) {
        if (ec) {
            return;
        }
        check_heartbeats();
        schedule_heartbeat();
    });
}

void AuthService::send_error(const SessionPtr& session, uint32_t seq, int code,
                             const std::string& msg) {
    Json::Value v;
    v["code"] = code;
    v["msg"] = msg;
    send_json(session, static_cast<uint16_t>(common::MsgType::kError), seq, v);
}

}  // namespace gateway
