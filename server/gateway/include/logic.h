#pragma once

#include "aoi_service.h"
#include "auth_service.h"
#include "config.h"
#include "redis_pool.h"
#include "session.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gateway {

class TcpServer;
class LogicWorker;

// 逻辑层：聚合业务 service + 逻辑线程池。
// 网络层（TcpServer）只负责解析拆包并把完整消息投递进来，本层异步处理所有业务消息。
class Logic {
public:
    using SessionPtr = std::shared_ptr<Session>;

    Logic(TcpServer& tcp, common::RedisPool& redis, common::JwtCfg jwt_cfg, int worker_count);
    ~Logic();

    // IO 线程调用：把消息投递到某个 LogicWorker 的队列（按 session 哈希，同一连接串行）
    void post_message(SessionPtr session, common::Packet pkt);
    // LogicWorker 调用：按 msg_type 分发到对应 service
    void dispatch(SessionPtr session, common::Packet pkt);
    // 连接断开时清理 AOI（由 IO 线程调用）
    void handle_disconnect(SessionPtr session, const std::string& reason);

private:
    AuthService auth_;
    AoiService aoi_;
    std::vector<std::unique_ptr<LogicWorker>> workers_;
};

}  // namespace gateway
