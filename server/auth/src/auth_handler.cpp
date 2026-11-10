#include "auth_handler.h"

#include "logger.h"
#include "password.h"

#include "gateway.grpc.pb.h"
#include "gateway.pb.h"

#include <grpcpp/grpcpp.h>

#include <cstdlib>
#include <ctime>
#include <string>

namespace auth {

namespace {

// gRPC 踢人：连接指定网关的 KickUser
void kick_user_via_grpc(const std::string& addr, uint32_t uid) {
    auto channel = grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
    auto stub = gatewayrpc::GatewayControl::NewStub(channel);
    gatewayrpc::KickUserRequest req;
    req.set_uid(uid);
    req.set_reason("已在别处登录");
    gatewayrpc::KickUserResponse resp;
    grpc::ClientContext ctx;
    grpc::Status status = stub->KickUser(&ctx, req, &resp);
    if (status.ok()) {
        LOG_INFO("gRPC 踢人成功 uid={} gateway={}", uid, addr);
    } else {
        LOG_WARN("gRPC 踢人失败 uid={} gateway={}: {}", uid, addr, status.error_message());
    }
}

}  // namespace

Json::Value handle_register(const Json::Value& req, AppState& st) {
    std::string username = req.isMember("username") ? req["username"].asString() : "";
    std::string password = req.isMember("password") ? req["password"].asString() : "";
    int role = req.isMember("role") ? req["role"].asInt() : 0;
    std::string nickname = req.isMember("nickname") ? req["nickname"].asString() : "";
    std::string tags = req.isMember("tags") ? req["tags"].asString() : "";

    Json::Value resp;
    if (username.empty() || password.empty()) {
        resp["code"] = 1;
        resp["msg"] = "用户名和密码不能为空";
        return resp;
    }

    std::string hash = common::hash_password(password);
    try {
        auto conn = st.pg.acquire();
        conn->execute(
            "INSERT INTO users(username, password_hash, role, nickname, tags) "
            "VALUES($1,$2,$3,$4,$5)",
            {username, hash, std::to_string(role), nickname, tags});
    } catch (const std::exception& e) {
        std::string msg = e.what();
        // 23505 = unique_violation（唯一约束冲突，即用户名已存在），按 SQLSTATE 判断，locale 无关
        if (msg.find("23505") != std::string::npos) {
            resp["code"] = 2;
            resp["msg"] = "用户名已存在";
        } else {
            LOG_ERROR("注册写库失败: {}", msg);
            resp["code"] = 5;
            resp["msg"] = "注册失败";
        }
        return resp;
    }

    resp["code"] = 0;
    LOG_INFO("注册成功: {}", username);
    return resp;
}

Json::Value handle_login(const Json::Value& req, AppState& st) {
    std::string username = req.isMember("username") ? req["username"].asString() : "";
    std::string password = req.isMember("password") ? req["password"].asString() : "";

    Json::Value resp;
    uint32_t uid = 0;
    std::string hash, nickname, tags;
    int role = 0;
    try {
        auto conn = st.pg.acquire();
        auto res = conn->query(
            "SELECT id, password_hash, nickname, role, tags FROM users WHERE username=$1",
            {username});
        if (res.rows() == 0) {
            resp["code"] = 3;
            resp["msg"] = "用户名或密码错误";
            return resp;
        }
        uid = static_cast<uint32_t>(std::strtoul(res.value(0, 0), nullptr, 10));
        hash = res.value(0, 1);
        nickname = res.value(0, 2);
        role = std::atoi(res.value(0, 3));
        tags = res.value(0, 4);
    } catch (const std::exception& e) {
        LOG_ERROR("登录查询失败: {}", e.what());
        resp["code"] = 5;
        resp["msg"] = "数据库错误";
        return resp;
    }

    if (!common::verify_password(password, hash)) {
        resp["code"] = 3;
        resp["msg"] = "用户名或密码错误";
        return resp;
    }

    int64_t now = static_cast<int64_t>(std::time(nullptr));
    int64_t exp = now + st.jwt_expire_seconds;
    std::string token = st.jwt.sign(uid, username, exp);

    // 会话缓存（供网关校验/重连）
    st.redis.set("session:" + std::to_string(uid), token, st.jwt_expire_seconds);

    // 异地登录互踢：查路由表 conn:{uid}，gRPC 通知对应网关踢旧连接
    auto old_gateway = st.redis.get("conn:" + std::to_string(uid));
    if (old_gateway) {
        kick_user_via_grpc(*old_gateway, uid);
    }

    resp["code"] = 0;
    resp["token"] = token;
    resp["uid"] = uid;
    resp["username"] = username;
    resp["nickname"] = nickname;
    resp["role"] = role;
    resp["tags"] = tags;
    LOG_INFO("登录成功: {} (uid={})", username, uid);
    return resp;
}

Json::Value handle_refresh(const Json::Value& req, AppState& st) {
    std::string token = req.isMember("token") ? req["token"].asString() : "";
    Json::Value resp;
    common::JwtPayload payload;
    if (!st.jwt.verify(token, payload)) {
        resp["code"] = 6;
        resp["msg"] = "无效或过期的 token";
        return resp;
    }
    int64_t now = static_cast<int64_t>(std::time(nullptr));
    int64_t exp = now + st.jwt_expire_seconds;
    std::string new_token = st.jwt.sign(payload.uid, payload.username, exp);
    resp["code"] = 0;
    resp["token"] = new_token;
    return resp;
}

}  // namespace auth
