#pragma once

#include "aoi_index.h"
#include "message_router.h"
#include "redis_pool.h"
#include "session.h"

#include <cstdint>
#include <memory>
#include <string>

namespace gateway {

class TcpServer;

// AOI 空间服务：坐标上报 + 九宫格索引 + 邻格广播（USER_ENTER / USER_LEAVE / POSITION_UPDATE）
class AoiService {
public:
    using SessionPtr = std::shared_ptr<Session>;

    AoiService(TcpServer& tcp, common::RedisPool& redis);

    // 处理坐标上报（POSITION_REPORT_REQ）：首次纳入网格回 nearby，后续邻格广播
    void handle_packet(SessionPtr session, const common::Packet& pkt);
    // 连接断开时清理 AOI：移出网格 + 广播 USER_LEAVE + 清 Redis 坐标
    void handle_disconnect(const SessionPtr& session, const std::string& reason);

private:
    void handle_position_report(const SessionPtr& session, uint32_t seq, const std::string& body);
    void remove_from_aoi(uint32_t uid);
    std::string nickname_of(uint32_t uid);
    void send_error(const SessionPtr& session, uint32_t seq, int code, const std::string& msg);

    TcpServer& tcp_;
    common::RedisPool& redis_;
    common::MessageRouter<SessionPtr> router_;
    AoiIndex aoi_;
};

}  // namespace gateway
