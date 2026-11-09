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
    return cfg;
}

Config Config::load(const std::string& path) {
    return from_json(read_file(path));
}

}  // namespace common
