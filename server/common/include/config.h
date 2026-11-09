#pragma once

#include <cstdint>
#include <string>

namespace common {

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

    // 从 JSON 文本解析 (供加载与单元测试复用), 失败抛 std::runtime_error
    static Config from_json(const std::string& text);

    // 从文件加载, 失败抛 std::runtime_error
    static Config load(const std::string& path);
};

}  // namespace common
