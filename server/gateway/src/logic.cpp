#include "logic.h"

#include "logic_worker.h"
#include "tcp_server.h"

#include <cstdint>
#include <functional>
#include <utility>

namespace gateway {

Logic::Logic(TcpServer& tcp, common::RedisPool& redis, common::JwtCfg jwt_cfg, int worker_count)
    : auth_(tcp, redis, jwt_cfg), aoi_(tcp, redis) {
    for (int i = 0; i < worker_count; ++i) {
        workers_.push_back(std::make_unique<LogicWorker>(this));
    }
}

Logic::~Logic() = default;

void Logic::post_message(SessionPtr session, common::Packet pkt) {
    // 按 session 指针哈希路由，保证同一连接的消息在同一个 worker 串行处理
    size_t idx = std::hash<const void*>()(session.get()) % workers_.size();
    workers_[idx]->post(LogicWorker::Task{std::move(session), std::move(pkt)});
}

void Logic::dispatch(SessionPtr session, common::Packet pkt) {
    if (pkt.msg_type == static_cast<uint16_t>(common::MsgType::kPositionReportReq)) {
        aoi_.handle_packet(session, pkt);
    } else {
        auth_.handle_packet(session, pkt);
    }
}

void Logic::handle_disconnect(SessionPtr session, const std::string& reason) {
    aoi_.handle_disconnect(session, reason);
}

}  // namespace gateway
