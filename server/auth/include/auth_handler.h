#pragma once

#include "jwt_auth.h"
#include "pg_pool.h"
#include "redis_pool.h"

#include <json/json.h>

namespace auth {

// AuthServer 共享状态：PG / Redis / JWT
struct AppState {
    common::PgPool& pg;
    common::RedisPool& redis;
    common::JwtAuth& jwt;
    int jwt_expire_seconds;
};

// 业务处理：返回 JSON（code=0 成功，非 0 错误）
Json::Value handle_register(const Json::Value& req, AppState& st);
Json::Value handle_login(const Json::Value& req, AppState& st);
Json::Value handle_refresh(const Json::Value& req, AppState& st);

}  // namespace auth
