#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// 前向声明 libpq 类型，避免在头文件暴露 libpq-fe.h
typedef struct pg_conn PGconn;
typedef struct pg_result PGresult;

namespace common {

// RAII 封装 PGresult
class PgResult {
public:
    explicit PgResult(PGresult* res);
    ~PgResult();
    PgResult(PgResult&& other) noexcept;
    PgResult& operator=(PgResult&& other) noexcept;
    PgResult(const PgResult&) = delete;
    PgResult& operator=(const PgResult&) = delete;

    int rows() const;
    int cols() const;
    const char* value(int row, int col) const;
    bool is_null(int row, int col) const;

private:
    PGresult* res_ = nullptr;
};

class PgPool;

// 连接对象：持有 PGconn + 最后活跃时间戳 + 健康状态。
// 析构时自动归还池子（健康）或 PQfinish（失效），无需额外的池化包装。
class PgConnection {
public:
    PgConnection(PGconn* conn, PgPool* pool, std::chrono::steady_clock::time_point last_used);
    ~PgConnection();
    PgConnection(PgConnection&& o) noexcept;
    PgConnection& operator=(PgConnection&& o) noexcept;
    PgConnection(const PgConnection&) = delete;
    PgConnection& operator=(const PgConnection&) = delete;

    bool is_ok() const;
    std::string error() const;

    // 执行无结果集 SQL（INSERT/UPDATE/DELETE），返回受影响行数；失败抛 std::runtime_error
    int execute(const std::string& sql, const std::vector<std::string>& params = {});
    // 查询，返回结果集；失败抛 std::runtime_error
    PgResult query(const std::string& sql, const std::vector<std::string>& params = {});

    // 心跳检测：执行 SELECT 1 判断连接是否存活（不抛异常）
    bool ping();

    // 每次成功使用后刷新最后活跃时间
    void touch() { last_used_ = std::chrono::steady_clock::now(); }
    std::chrono::steady_clock::time_point last_used() const { return last_used_; }

private:
    void release_own();  // 归还或释放当前持有的连接（供析构/移动赋值复用）

    PGconn* conn_ = nullptr;
    PgPool* pool_ = nullptr;
    std::chrono::steady_clock::time_point last_used_;
};

// PostgreSQL 连接池（基于 libpq）：
//   - 构造时预建 pool_size 个连接
//   - 借用/归还（acquire / 连接析构自动归还）+ 条件变量阻塞
//   - 后台心跳线程：只对「空闲超时」的连接 SELECT 1，失效释放、自动补齐
class PgPool {
public:
    struct Options {
        std::string host = "127.0.0.1";
        uint16_t port = 5432;
        std::string user = "postgres";
        std::string password;
        std::string database = "smart_campus";
        int pool_size = 8;
    };

    explicit PgPool(Options opt);
    ~PgPool();

    // 借一个连接（阻塞），池不可用时抛 std::runtime_error
    PgConnection acquire();

    bool is_available() const { return up_.load(); }

private:
    friend class PgConnection;                          // 允许连接对象归还

    PGconn* create();                                   // 新建连接，失败返回 nullptr
    void release(PGconn* conn,
                 std::chrono::steady_clock::time_point last_used);  // 归还健康连接
    void health_check_loop();

    Options opt_;
    std::deque<PgConnection> idle_;                     // 空闲连接（连接对象本身）
    std::mutex mutex_;
    std::condition_variable cv_;
    // 服务可达性：由 create、health_check 维护
    std::atomic_bool up_{false};
    std::atomic_bool stop_{false};
    std::thread health_thread_;
};

}  // namespace common
