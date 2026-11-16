#include "config.h"
#include "kick_service.h"
#include "logger.h"
#include "logic.h"
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
using gateway::KickService;
using gateway::Logic;
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
        // Redis（路由表 conn:{uid} / 会话缓存 session:{uid} / 坐标缓存 coord:{uid}）
        common::RedisPool::Options redis_opt;
        redis_opt.host = cfg.redis.host;
        redis_opt.port = cfg.redis.port;
        redis_opt.password = cfg.redis.password;
        redis_opt.pool_size = cfg.redis.pool_size;
        common::RedisPool redis(redis_opt);
        LOG_INFO("Redis {}", redis.is_available() ? "已连接" : "不可用（会话/互踢/坐标降级）");

        // 网关 gRPC 地址（写入路由表，AuthServer 据此踢人）
        std::string gateway_grpc_addr =
            cfg.gateway_grpc.host + ":" + std::to_string(cfg.gateway_grpc.port);

        // 1. TcpServer：连接管理（连接表 + 心跳 + 互踢 + 路由表 conn:{uid}）
        TcpServer::Options opt;
        opt.host = cfg.host;
        opt.port = cfg.port;
        TcpServer server(io, opt, redis, cfg.heartbeat, gateway_grpc_addr);

        // 2. Logic：逻辑层（鉴权 + AOI + 逻辑线程池，异步处理业务消息）
        Logic logic(server, redis, cfg.jwt, 4);

        // 3. KickService：gRPC 控制面（异地互踢）
        KickService kick_service(server);
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

        // 组装回调：网络层只解析并投递，业务消息交给逻辑层异步处理
        server.on_connected = [](const auto& s) { LOG_INFO("客户端接入: {}", s->remote()); };
        server.on_packet = [&logic](const auto& s, const common::Packet& pkt) {
            logic.post_message(s, pkt);
        };
        server.on_disconnected = [&logic](const auto& s, const std::string& reason) {
            logic.handle_disconnect(s, reason);
        };
        server.start();

        LOG_INFO("=== 智慧校园服务端 (M2 AOI 空间索引 + 地图) 启动 ===");
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
