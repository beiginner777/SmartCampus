#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
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

// RAII 封装单个 PGconn
class PgConnection {
public:
    explicit PgConnection(PGconn* conn);
    ~PgConnection();
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

private:
    PGconn* conn_ = nullptr;
};

class PgPool;

// 池化连接：析构时自动归还连接池
class PooledPgConnection {
public:
    PooledPgConnection(std::shared_ptr<PgConnection> conn, PgPool* pool);
    ~PooledPgConnection();
    PooledPgConnection(PooledPgConnection&& o) noexcept;
    PooledPgConnection& operator=(PooledPgConnection&& o) noexcept;
    PooledPgConnection(const PooledPgConnection&) = delete;
    PooledPgConnection& operator=(const PooledPgConnection&) = delete;

    PgConnection* operator->() { return conn_.get(); }
    PgConnection& operator*() { return *conn_; }
    explicit operator bool() const { return conn_ != nullptr; }

private:
    std::shared_ptr<PgConnection> conn_;
    PgPool* pool_ = nullptr;
};

// PostgreSQL 连接池（基于 libpq）：
//   - 构造时预建 pool_size 个连接
//   - 借用/归还（acquire/release）+ 条件变量阻塞
//   - 后台心跳保活线程：SELECT 1 剔除死连接并自动补齐
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
    PooledPgConnection acquire();

    bool is_available() const { return available_.load(); }

private:
    friend class PooledPgConnection;             // 允许其归还连接
    std::shared_ptr<PgConnection> create();      // 新建连接，失败返回 nullptr（不抛）
    void release(std::shared_ptr<PgConnection> conn);
    void health_check_loop();

    Options opt_;
    std::vector<std::shared_ptr<PgConnection>> idle_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic_bool available_{false};
    std::atomic_bool stop_{false};
    std::thread health_thread_;
};

}  // namespace common
