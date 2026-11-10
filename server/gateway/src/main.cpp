#include "auth_service.h"
#include "config.h"
#include "kick_service.h"
#include "logger.h"
#include "redis_pool.h"
#include "tcp_server.h"

#include <boost/asio.hpp>
#include <grpcpp/grpcpp.h>

#include <csignal>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using common::Config;
using common::Logger;
using gateway::AuthService;
using gateway::KickService;
using gateway::TcpServer;

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

    Config cfg;
    try {
        cfg = Config::load(cfg_path);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "加载配置文件失败: %s\n", e.what());
        return 1;
    }

    try {
        Logger::init(cfg.log_level, cfg.log_file);
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
        // Redis（互踢 PubSub 协调；缺失时互踢降级）
        common::RedisPool::Options redis_opt;
        redis_opt.host = cfg.redis.host;
        redis_opt.port = cfg.redis.port;
        redis_opt.password = cfg.redis.password;
        redis_opt.pool_size = cfg.redis.pool_size;
        common::RedisPool redis(redis_opt);
        LOG_INFO("Redis {}", redis.is_available() ? "已连接" : "不可用（互踢降级）");

        // 网关 gRPC 地址（写入路由表，AuthServer 据此踢人）
        std::string gateway_grpc_addr =
            cfg.gateway_grpc.host + ":" + std::to_string(cfg.gateway_grpc.port);

        // 鉴权/会话管理（JWT 无状态校验，注册/登录在 AuthServer）
        AuthService auth(io, redis, cfg.jwt, cfg.heartbeat, gateway_grpc_addr);
        auth.start();

        // gRPC 控制面（踢人）
        KickService kick_service(auth);
        grpc::ServerBuilder grpc_builder;
        grpc_builder.AddListeningPort("0.0.0.0:" + std::to_string(cfg.gateway_grpc.port),
                                      grpc::InsecureServerCredentials());
        grpc_builder.RegisterService(&kick_service);
        std::unique_ptr<grpc::Server> grpc_server(grpc_builder.BuildAndStart());
        if (!grpc_server) {
            LOG_ERROR("gRPC 控制面启动失败");
            return 1;
        }
        LOG_INFO("gRPC 控制面监听 0.0.0.0:{}", cfg.gateway_grpc.port);
        std::thread grpc_thread([&grpc_server] { grpc_server->Wait(); });

        // TCP 接入
        TcpServer::Options opt;
        opt.host = cfg.host;
        opt.port = cfg.port;
        TcpServer server(io, opt);
        server.on_connected = [](const auto& s) { LOG_INFO("客户端接入: {}", s->remote()); };
        server.on_packet = [&auth](const auto& s, const common::Packet& pkt) {
            auth.handle_packet(s, pkt);
        };
        server.on_disconnected = [&auth](const auto& s, const std::string& reason) {
            auth.handle_disconnect(s, reason);
        };
        server.start();

        LOG_INFO("=== 智慧校园服务端 (M1 通信底座 + 鉴权) 启动 ===");
        LOG_INFO("监听 {}:{}  工作线程 {}", cfg.host, cfg.port, cfg.thread_count);

        std::vector<std::thread> pool;
        pool.reserve(static_cast<size_t>(cfg.thread_count));
        for (int i = 0; i < cfg.thread_count; ++i) {
            pool.emplace_back([&io] { io.run(); });
        }
        for (auto& t : pool) {
            t.join();
        }

        grpc_server->Shutdown();
        grpc_thread.join();
    } catch (const std::exception& e) {
        LOG_ERROR("服务端运行异常: {}", e.what());
        return 1;
    }

    LOG_INFO("服务端已退出");
    return 0;
}
