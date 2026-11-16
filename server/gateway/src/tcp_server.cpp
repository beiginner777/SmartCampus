#include "tcp_server.h"

#include "logger.h"

#include <json/json.h>

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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

}  // namespace

TcpServer::TcpServer(boost::asio::io_context& io, Options opt, common::RedisPool& redis,
                     common::HeartbeatCfg hb, const std::string& gateway_grpc_addr)
    : io_(io),
      opt_(std::move(opt)),
      acceptor_(io, tcp::endpoint(boost::asio::ip::make_address(opt_.host), opt_.port)),
      redis_(redis),
      hb_(hb),
      gateway_grpc_addr_(gateway_grpc_addr) {
    acceptor_.set_option(tcp::acceptor::reuse_address(true));
}

TcpServer::~TcpServer() = default;

void TcpServer::start() {
    do_accept();
    schedule_heartbeat();
    LOG_INFO("TCP 服务监听 {}:{}", opt_.host, opt_.port);
}

void TcpServer::stop() {
    boost::system::error_code ec;
    acceptor_.close(ec);
}

void TcpServer::do_accept() {
    auto session = std::make_shared<Session>(io_);
    acceptor_.async_accept(session->socket(), [this, session](const boost::system::error_code& ec) {
        if (ec) {
            if (ec != boost::asio::error::operation_aborted) {
                LOG_ERROR("accept 失败: {}", ec.message());
            }
            return;
        }
        // 回调只捕获 this（TcpServer），session 由 shared_ptr 参数传入，避免循环引用
        session->on_packet = [this](SessionPtr s, const common::Packet& pkt) {
            if (on_packet) {
                on_packet(s, pkt);
            }
        };
        session->on_disconnect = [this](SessionPtr s, const std::string& reason) {
            uint32_t uid = s->uid();
            if (uid != 0) {
                close_session(uid, s, reason);  // 统一下线清理
            }
        };
        session->start();
        if (on_connected) {
            on_connected(session);
        }
        do_accept();
    });
}

void TcpServer::register_session(uint32_t uid, SessionPtr session) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_[uid] = session;
    }
    // 写路由表：uid → 本网关 gRPC 地址（AuthServer 登录时据此精准踢人）
    redis_.set("conn:" + std::to_string(uid), gateway_grpc_addr_, hb_.timeout_seconds);
}

void TcpServer::unregister_session(uint32_t uid, SessionPtr session) {
    bool offline = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(uid);
        if (it != sessions_.end()) {
            auto s = it->second.lock();
            if (!s || s.get() == session.get()) {
                sessions_.erase(it);
                offline = true;
            }
        }
    }
    if (offline) {
        redis_.del("conn:" + std::to_string(uid));
    }
}

void TcpServer::close_session(uint32_t uid, SessionPtr session, const std::string& reason) {
    unregister_session(uid, session);  // 清连接表 + 删 conn:{uid}
    if (on_disconnected) {
        on_disconnected(session, reason);  // 通知 Logic → AoiService 清 AOI
    }
}

TcpServer::SessionPtr TcpServer::find_session(uint32_t uid) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(uid);
    if (it == sessions_.end()) {
        return nullptr;
    }
    return it->second.lock();
}

void TcpServer::send_to(uint32_t uid, uint16_t msg_type, const std::string& body) {
    auto s = find_session(uid);
    if (s) {
        s->send_packet(msg_type, 0, body);
    }
}

void TcpServer::refresh_conn(uint32_t uid) {
    // 连接仍在线，conn:{uid} 续期，保证异地互踢的 gRPC 快路径有效
    redis_.set("conn:" + std::to_string(uid), gateway_grpc_addr_, hb_.timeout_seconds);
}

void TcpServer::kick_uid(uint32_t uid, const std::string& reason) {
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
        redis_.del("conn:" + std::to_string(uid));
        kick(target, reason);
    }
}

void TcpServer::kick(const SessionPtr& session, const std::string& reason) {
    Json::Value v;
    v["reason"] = reason;
    session->send_packet(static_cast<uint16_t>(common::MsgType::kKickNotify), 0,
                         json_to_string(v));
    // TODO:时间间隔怎么确定？
    // 短暂延迟后关闭，确保 KICK 通知送达
    auto timer =
        std::make_shared<boost::asio::steady_timer>(io_, std::chrono::milliseconds(200));
    timer->async_wait([session](const boost::system::error_code& ec) {
        if (!ec) {
            session->stop();
        }
    });
}

void TcpServer::check_heartbeats() {
    auto now = std::chrono::steady_clock::now();
    std::vector<SessionPtr> to_kick;
    {
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
                to_kick.push_back(s);
                it = sessions_.erase(it);
            } else {
                ++it;
            }
        }
    }
    // 锁外处理：踢人 + 删路由表（kick 内部不取 sessions_ 锁）
    for (auto& s : to_kick) {
        LOG_INFO("用户 {} (uid={}) 心跳超时，踢下线", s->username(), s->uid());
        redis_.del("conn:" + std::to_string(s->uid()));
        kick(s, "心跳超时");
    }
}

void TcpServer::schedule_heartbeat() {
    if (hb_.interval_seconds <= 0) {
        LOG_CRITICAL("心跳检测的间隔时间(单位:s) <= 0");
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

}  // namespace gateway
