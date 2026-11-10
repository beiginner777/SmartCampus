#pragma once

#include "auth_service.h"

#include "gateway.grpc.pb.h"
#include "gateway.pb.h"

namespace gateway {

// 网关控制面 gRPC 服务：接收 AuthServer 的踢人请求（异地登录互踢）
class KickService final : public gatewayrpc::GatewayControl::Service {
public:
    explicit KickService(AuthService& auth) : auth_(auth) {}

    ::grpc::Status KickUser(::grpc::ServerContext* context,
                            const ::gatewayrpc::KickUserRequest* request,
                            ::gatewayrpc::KickUserResponse* response) override;

private:
    AuthService& auth_;
};

}  // namespace gateway
