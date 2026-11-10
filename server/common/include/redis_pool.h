#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>

// 前向声明 hiredis 类型，避免在头文件暴露 hiredis.h
typedef struct redisContext redisContext;

namespace common {

class RedisPool;

// 池化连接：析构时自动归还连接池
class PooledRedisConnection {
public:
    PooledRedisConnection(redisContext* ctx, RedisPool* pool) : ctx_(ctx), pool_(pool) {}
    ~PooledRedisConnection();
    PooledRedisConnection(PooledRedisConnection&& o) noexcept;
    PooledRedisConnection& operator=(PooledRedisConnection&& o) noexcept;
    PooledRedisConnection(const PooledRedisConnection&) = delete;
    PooledRedisConnection& operator=(const PooledRedisConnection&) = delete;

    redisContext* get() const { return ctx_; }
    explicit operator bool() const { return ctx_ != nullptr; }

private:
    redisContext* ctx_ = nullptr;
    RedisPool* pool_ = nullptr;
};

// Redis 连接池（基于 hiredis）：
//   - 构造时预建 pool_size 个连接并 AUTH
//   - 借用/归还（acquire/release）+ 条件变量阻塞
//   - 后台心跳保活线程：PING 剔除失效连接并自动补齐
class RedisPool {
public:
    struct Options {
        std::string host = "127.0.0.1";
        uint16_t port = 6380;
        std::string password;   // requirepass 时使用
        int pool_size = 8;
    };

    explicit RedisPool(Options opt);
    ~RedisPool();

    // 业务操作（内部借用连接，线程安全）
    bool set(const std::string& key, const std::string& value, int ttl_seconds = 0);
    std::optional<std::string> get(const std::string& key);
    bool del(const std::string& key);

    bool is_available() const { return available_.load(); }

private:
    friend class PooledRedisConnection;            // 允许其归还连接
    redisContext* create();                        // 新建连接并 AUTH，失败返回 nullptr
    PooledRedisConnection acquire();               // 借一个连接（无可用且池空时返回空）
    void release(redisContext* ctx);               // 归还连接
    void health_check_loop();                      // 心跳保活线程

    Options opt_;
    std::queue<redisContext*> pool_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic_bool available_{false};
    std::atomic_bool stop_{false};
    std::thread health_thread_;
};

}  // namespace common
