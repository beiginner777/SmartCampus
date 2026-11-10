#include "kick_service.h"

#include "logger.h"

namespace gateway {

::grpc::Status KickService::KickUser(::grpc::ServerContext* /*context*/,
                                     const ::gatewayrpc::KickUserRequest* request,
                                     ::gatewayrpc::KickUserResponse* response) {
    uint32_t uid = request->uid();
    std::string reason = request->reason().empty() ? "已在别处登录" : request->reason();
    LOG_INFO("收到 gRPC 踢人请求 uid={} reason={}", uid, reason);
    auth_.kick_uid(uid, reason);
    response->set_code(0);
    return ::grpc::Status::OK;
}

}  // namespace gateway
