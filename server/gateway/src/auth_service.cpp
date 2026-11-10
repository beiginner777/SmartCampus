#include "auth_service.h"

#include "logger.h"
#include "password.h"

#include <json/json.h>

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <utility>

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

AuthService::AuthService(boost::asio::io_context& io, common::PgPool& pg,
                         common::RedisPool& redis, common::JwtCfg jwt_cfg,
                         common::HeartbeatCfg hb)
    : io_(io),
      pg_(pg),
      redis_(redis),
      jwt_(jwt_cfg.secret),
      jwt_expire_seconds_(jwt_cfg.expire_seconds),
      hb_(hb) {
    using common::MsgType;
    router_.register_handler(static_cast<uint16_t>(MsgType::kRegisterReq),
                             [this](SessionPtr s, uint32_t seq, const std::string& b) {
                                 handle_register(s, seq, b);
                             });
    router_.register_handler(static_cast<uint16_t>(MsgType::kLoginReq),
                             [this](SessionPtr s, uint32_t seq, const std::string& b) {
                                 handle_login(s, seq, b);
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
    // 会话保留在 Redis（token 有效期内可重连）
}

void AuthService::handle_register(const SessionPtr& session, uint32_t seq,
                                  const std::string& body) {
    Json::Value req;
    Json::Reader reader;
    if (!reader.parse(body, req)) {
        send_error(session, seq, 1, "参数解析失败");
        return;
    }
    std::string username = req.isMember("username") ? req["username"].asString() : "";
    std::string password = req.isMember("password") ? req["password"].asString() : "";
    int role = req.isMember("role") ? req["role"].asInt() : 0;
    std::string nickname = req.isMember("nickname") ? req["nickname"].asString() : "";
    std::string tags = req.isMember("tags") ? req["tags"].asString() : "";

    if (username.empty() || password.empty()) {
        send_error(session, seq, 1, "用户名和密码不能为空");
        return;
    }

    std::string hash = common::hash_password(password);
    try {
        auto conn = pg_.acquire();
        conn->execute(
            "INSERT INTO users(username, password_hash, role, nickname, tags) "
            "VALUES($1,$2,$3,$4,$5)",
            {username, hash, std::to_string(role), nickname, tags});
    } catch (const std::exception& e) {
        std::string msg = e.what();
        if (msg.find("duplicate") != std::string::npos ||
            msg.find("unique") != std::string::npos) {
            send_error(session, seq, 2, "用户名已存在");
        } else {
            LOG_ERROR("注册写库失败: {}", msg);
            send_error(session, seq, 5, "注册失败");
        }
        return;
    }

    Json::Value resp;
    resp["code"] = 0;
    send_json(session, static_cast<uint16_t>(common::MsgType::kRegisterResp), seq, resp);
    LOG_INFO("注册成功: {}", username);
}

void AuthService::handle_login(const SessionPtr& session, uint32_t seq,
                               const std::string& body) {
    Json::Value req;
    Json::Reader reader;
    if (!reader.parse(body, req)) {
        send_error(session, seq, 1, "参数解析失败");
        return;
    }
    std::string username = req.isMember("username") ? req["username"].asString() : "";
    std::string password = req.isMember("password") ? req["password"].asString() : "";

    uint32_t uid = 0;
    std::string hash, nickname, tags;
    int role = 0;
    try {
        auto conn = pg_.acquire();
        auto res = conn->query(
            "SELECT id, password_hash, nickname, role, tags FROM users WHERE username=$1",
            {username});
        if (res.rows() == 0) {
            send_error(session, seq, 3, "用户名或密码错误");
            return;
        }
        uid = static_cast<uint32_t>(std::strtoul(res.value(0, 0), nullptr, 10));
        hash = res.value(0, 1);
        nickname = res.value(0, 2);
        role = std::atoi(res.value(0, 3));
        tags = res.value(0, 4);
    } catch (const std::exception& e) {
        LOG_ERROR("登录查询失败: {}", e.what());
        send_error(session, seq, 5, "数据库错误");
        return;
    }

    if (!common::verify_password(password, hash)) {
        send_error(session, seq, 3, "用户名或密码错误");
        return;
    }

    int64_t now = static_cast<int64_t>(std::time(nullptr));
    int64_t exp = now + jwt_expire_seconds_;
    std::string token = jwt_.sign(uid, username, exp);

    redis_.set("session:" + std::to_string(uid), token, jwt_expire_seconds_);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(uid);
        if (it != sessions_.end()) {
            auto old = it->second.lock();
            if (old && old.get() != session.get()) {
                LOG_INFO("用户 {} (uid={}) 异地登录，踢掉旧连接", username, uid);
                kick(old, "已在别处登录");
            }
        }
        sessions_[uid] = session;
    }

    session->set_auth(uid, username);
    session->touch_heartbeat();

    Json::Value resp;
    resp["code"] = 0;
    resp["token"] = token;
    resp["uid"] = uid;
    resp["username"] = username;
    resp["nickname"] = nickname;
    resp["role"] = role;
    resp["tags"] = tags;
    send_json(session, static_cast<uint16_t>(common::MsgType::kLoginResp), seq, resp);
    LOG_INFO("登录成功: {} (uid={})", username, uid);
}

void AuthService::handle_logout(const SessionPtr& session, uint32_t seq,
                                const std::string& /*body*/) {
    if (!session->authenticated()) {
        send_error(session, seq, 4, "未登录");
        return;
    }
    uint32_t uid = session->uid();
    redis_.del("session:" + std::to_string(uid));
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
