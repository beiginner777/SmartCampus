#include "logger.h"

namespace common {

void Logger::init(const std::string& level, const std::string& log_file) {
    auto level_enum = spdlog::level::from_str(level);

    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(level_enum);

    std::vector<spdlog::sink_ptr> sinks{console_sink};

    if (!log_file.empty()) {
        try {
            auto file_sink =
                std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file, /*truncate=*/true);
            file_sink->set_level(spdlog::level::trace);
            sinks.push_back(file_sink);
        } catch (const spdlog::spdlog_ex& e) {
            spdlog::warn("无法创建日志文件 {}: {}", log_file, e.what());
        }
    }

    auto logger = std::make_shared<spdlog::logger>("smart_campus", sinks.begin(), sinks.end());
    logger->set_level(level_enum);
    logger->flush_on(spdlog::level::info);
    spdlog::set_default_logger(logger);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
}

}  // namespace common
