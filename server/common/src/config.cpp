#include "config.h"

#include <json/json.h>

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace common {

namespace {

std::string read_file(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        throw std::runtime_error("无法打开配置文件: " + path);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// 若存在 key 且为对象，则调用 fn 解析
template <typename Fn>
void parse_object(const Json::Value& root, const char* key, Fn&& fn) {
    if (root.isMember(key) && root[key].isObject()) {
        fn(root[key]);
    }
}

}  // namespace

Config Config::from_json(const std::string& text) {
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(text, root)) {
        throw std::runtime_error("JSON 解析失败: " + reader.getFormatedErrorMessages());
    }

    Config cfg;
    if (root.isMember("host")) {
        cfg.host = root["host"].asString();
    }
    if (root.isMember("port")) {
        cfg.port = static_cast<uint16_t>(root["port"].asUInt());
    }
    if (root.isMember("thread_count")) {
        cfg.thread_count = root["thread_count"].asInt();
    }
    if (root.isMember("log_level")) {
        cfg.log_level = root["log_level"].asString();
    }
    if (root.isMember("log_file")) {
        cfg.log_file = root["log_file"].asString();
    }

    parse_object(root, "jwt", [&](const Json::Value& o) {
        if (o.isMember("secret")) {
            cfg.jwt.secret = o["secret"].asString();
        }
        if (o.isMember("expire_seconds")) {
            cfg.jwt.expire_seconds = o["expire_seconds"].asInt();
        }
    });

    parse_object(root, "heartbeat", [&](const Json::Value& o) {
        if (o.isMember("interval_seconds")) {
            cfg.heartbeat.interval_seconds = o["interval_seconds"].asInt();
        }
        if (o.isMember("timeout_seconds")) {
            cfg.heartbeat.timeout_seconds = o["timeout_seconds"].asInt();
        }
    });

    parse_object(root, "postgres", [&](const Json::Value& o) {
        if (o.isMember("host")) {
            cfg.postgres.host = o["host"].asString();
        }
        if (o.isMember("port")) {
            cfg.postgres.port = static_cast<uint16_t>(o["port"].asUInt());
        }
        if (o.isMember("user")) {
            cfg.postgres.user = o["user"].asString();
        }
        if (o.isMember("password")) {
            cfg.postgres.password = o["password"].asString();
        }
        if (o.isMember("database")) {
            cfg.postgres.database = o["database"].asString();
        }
        if (o.isMember("pool_size")) {
            cfg.postgres.pool_size = o["pool_size"].asInt();
        }
    });

    parse_object(root, "redis", [&](const Json::Value& o) {
        if (o.isMember("host")) {
            cfg.redis.host = o["host"].asString();
        }
        if (o.isMember("port")) {
            cfg.redis.port = static_cast<uint16_t>(o["port"].asUInt());
        }
        if (o.isMember("password")) {
            cfg.redis.password = o["password"].asString();
        }
        if (o.isMember("pool_size")) {
            cfg.redis.pool_size = o["pool_size"].asInt();
        }
    });

    return cfg;
}

Config Config::load(const std::string& path) {
    return from_json(read_file(path));
}

}  // namespace common
