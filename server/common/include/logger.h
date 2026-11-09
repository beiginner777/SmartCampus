#pragma once

#include <memory>
#include <string>
#include <vector>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace common {

// 日志封装：基于 spdlog，统一初始化默认 logger
// 输出到控制台 (带颜色) + 可选文件，供 gateway / logic 等节点复用
class Logger {
public:
    // 初始化默认 logger：level 取值 trace/debug/info/warn/error/critical
    // log_file 为空则仅输出到控制台
    static void init(const std::string& level, const std::string& log_file);
};

}  // namespace common

// 全局日志宏：走 spdlog 默认 logger
#define LOG_TRACE(...)    ::spdlog::trace(__VA_ARGS__)
#define LOG_DEBUG(...)    ::spdlog::debug(__VA_ARGS__)
#define LOG_INFO(...)     ::spdlog::info(__VA_ARGS__)
#define LOG_WARN(...)     ::spdlog::warn(__VA_ARGS__)
#define LOG_ERROR(...)    ::spdlog::error(__VA_ARGS__)
#define LOG_CRITICAL(...) ::spdlog::critical(__VA_ARGS__)
