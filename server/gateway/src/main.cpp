#include "config.h"
#include "logger.h"
#include "tcp_server.h"

#include <boost/asio.hpp>

#include <csignal>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using common::Config;
using common::Logger;
using gateway::TcpServer;

namespace {

boost::asio::io_context* g_io = nullptr;

// 仅做 stop 标记, 不在信号处理函数内调用非异步信号安全的日志
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
    // SetConsoleCP(CP_UTF8);
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
        TcpServer::Options opt;
        opt.host = cfg.host;
        opt.port = cfg.port;

        TcpServer server(io, opt);
        server.on_connected = [](const std::string& ep) { LOG_INFO("客户端接入: {}", ep); };
        server.on_received = [](const std::string& ep, const std::string& data) {
            LOG_INFO("收到 [{}] {} 字节, 回显", ep, data.size());
        };
        server.on_disconnected = [](const std::string& ep, const std::string& reason) {
            LOG_INFO("客户端断开 [{}]: {}", ep, reason);
        };

        server.start();
        LOG_INFO("=== 智慧校园服务端 EchoServer (M0) 启动 ===");
        LOG_INFO("监听 {}:{}  工作线程 {}", cfg.host, cfg.port, cfg.thread_count);

        std::vector<std::thread> pool;
        pool.reserve(static_cast<size_t>(cfg.thread_count));
        for (int i = 0; i < cfg.thread_count; ++i) {
            pool.emplace_back([&io] { io.run(); });
        }
        for (auto& t : pool) {
            t.join();
        }
    } catch (const std::exception& e) {
        LOG_ERROR("服务端运行异常: {}", e.what());
        return 1;
    }

    LOG_INFO("服务端已退出");
    return 0;
}
