#pragma once

#include <cstdint>
#include <string>

namespace common {

// JWT 配置
struct JwtCfg {
    std::string secret = "change-me";
    int expire_seconds = 7200;
};

// 心跳保活配置
struct HeartbeatCfg {
    int interval_seconds = 30;  // 客户端心跳间隔（约定）
    int timeout_seconds = 90;   // 超过该值未心跳则踢下线
};

// PostgreSQL 配置
struct PostgresCfg {
    std::string host = "127.0.0.1";
    uint16_t port = 5432;
    std::string user = "postgres";
    std::string password;
    std::string database = "smart_campus";
    int pool_size = 8;
};

// Redis 配置
struct RedisCfg {
    std::string host = "127.0.0.1";
    uint16_t port = 6380;
    std::string password;   // requirepass 时使用
    int pool_size = 8;
};

// 服务端配置：从 JSON 文件加载 (third-party/jsoncpp)
// 字段见 config/server.json
struct Config {
    // 网关监听
    std::string host = "0.0.0.0";
    uint16_t port = 9000;
    int thread_count = 4;

    // 日志
    std::string log_level = "info";
    std::string log_file = "logs/server.log";

    // 各子系统配置
    JwtCfg jwt;
    HeartbeatCfg heartbeat;
    PostgresCfg postgres;
    RedisCfg redis;

    // 从 JSON 文本解析 (供加载与单元测试复用), 失败抛 std::runtime_error
    static Config from_json(const std::string& text);

    // 从文件加载, 失败抛 std::runtime_error
    static Config load(const std::string& path);
};

}  // namespace common
