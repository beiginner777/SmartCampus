#include "auth_handler.h"
#include "http_server.h"

#include "config.h"
#include "jwt_auth.h"
#include "logger.h"
#include "pg_pool.h"
#include "redis_pool.h"

#include <boost/asio.hpp>

#include <csignal>
#include <cstdio>
#include <string>

namespace {

boost::asio::io_context* g_io = nullptr;

// 仅做 stop 标记，不在信号处理函数内调用非异步信号安全的日志
void signal_handler(int) {
    if (g_io) {
        g_io->stop();
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string cfg_path = "config/server.json";
    if (argc > 1) {
        cfg_path = argv[1];
    }

#ifdef _WIN32
    // 把控制台输入/输出代码页切到 UTF-8，解决 Windows 下中文乱码
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    common::Config cfg;
    try {
        cfg = common::Config::load(cfg_path);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "加载配置文件失败: %s\n", e.what());
        return 1;
    }
    try {
        common::Logger::init(cfg.log_level, cfg.log_file);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "日志初始化失败: %s\n", e.what());
        return 1;
    }

    boost::asio::io_context io;
    g_io = &io;
    std::signal(SIGINT, signal_handler);
#ifdef SIGTERM
    std::signal(SIGTERM, signal_handler);
#endif

    try {
        common::PgPool::Options pg_opt;
        pg_opt.host = cfg.postgres.host;
        pg_opt.port = cfg.postgres.port;
        pg_opt.user = cfg.postgres.user;
        pg_opt.password = cfg.postgres.password;
        pg_opt.database = cfg.postgres.database;
        pg_opt.pool_size = cfg.postgres.pool_size;
        common::PgPool pg(pg_opt);
        if (!pg.is_available()) {
            LOG_ERROR("PostgreSQL 连接失败，请检查数据库服务与配置");
            return 1;
        }
        LOG_INFO("PostgreSQL 已连接 ({})", cfg.postgres.database);

        common::RedisPool::Options redis_opt;
        redis_opt.host = cfg.redis.host;
        redis_opt.port = cfg.redis.port;
        redis_opt.password = cfg.redis.password;
        redis_opt.pool_size = cfg.redis.pool_size;
        common::RedisPool redis(redis_opt);
        LOG_INFO("Redis {}", redis.is_available() ? "已连接" : "不可用（会话/互踢降级）");

        common::JwtAuth jwt(cfg.jwt.secret);
        auth::AppState st{pg, redis, jwt, cfg.jwt.expire_seconds};

        auto addr = boost::asio::ip::make_address(cfg.auth.host);
        auth::HttpServer server(io, boost::asio::ip::tcp::endpoint(addr, cfg.auth.port), st);

        LOG_INFO("=== AuthServer (beast HTTP) 启动 ===");
        LOG_INFO("监听 {}:{}", cfg.auth.host, cfg.auth.port);

        io.run();
    } catch (const std::exception& e) {
        LOG_ERROR("AuthServer 运行异常: {}", e.what());
        return 1;
    }

    LOG_INFO("AuthServer 已退出");
    return 0;
}
