#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

// 前向声明 hiredis 类型，避免在头文件暴露 hiredis.h
typedef struct redisContext redisContext;

namespace common {

class RedisPool;

// 连接对象：持有底层连接 + 最后活跃时间戳 + 健康状态。
// 空闲时作为池子的元素存在；被借出后析构自动归还（健康）或释放（失效）。
class RedisConnection {
public:
    RedisConnection(redisContext* ctx, RedisPool* pool,
                    std::chrono::steady_clock::time_point last_used);
    ~RedisConnection();
    RedisConnection(RedisConnection&& o) noexcept;
    RedisConnection& operator=(RedisConnection&& o) noexcept;
    RedisConnection(const RedisConnection&) = delete;
    RedisConnection& operator=(const RedisConnection&) = delete;

    redisContext* get() const { return ctx_; }
    explicit operator bool() const { return ctx_ != nullptr; }

    // 每次成功使用后刷新最后活跃时间
    void touch() { last_used_ = std::chrono::steady_clock::now(); }
    // 标记本次使用失败：析构时直接释放连接而非归还
    void mark_bad() { healthy_ = false; }
    std::chrono::steady_clock::time_point last_used() const { return last_used_; }

private:
    void release_own();  // 归还或释放当前持有的连接（供析构/移动赋值复用）

    redisContext* ctx_ = nullptr;
    RedisPool* pool_ = nullptr;
    std::chrono::steady_clock::time_point last_used_;
    bool healthy_ = true;
};

// Redis 连接池（基于 hiredis）：
//   - 构造时预建 pool_size 个连接并 AUTH
//   - 借用/归还（acquire / 连接析构自动归还）+ 条件变量阻塞
//   - 后台心跳线程：只对「空闲超时」的连接 PING，失效释放、自动补齐
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

    // 发布消息到频道
    bool publish(const std::string& channel, const std::string& message);
    // 订阅频道（独立连接 + 后台线程），收到消息回调 on_message
    void subscribe(const std::string& channel, std::function<void(const std::string&)> on_message);

    bool is_available() const { return up_.load(); }

private:
    friend class RedisConnection;                          // 允许连接对象归还

    redisContext* create();                                // 新建连接并 AUTH，失败返回 nullptr
    RedisConnection acquire();                             // 借一个连接（无可用且池空时返回空）
    void release(redisContext* ctx,
                 std::chrono::steady_clock::time_point last_used);  // 归还健康连接
    void health_check_loop();                              // 心跳保活线程

    Options opt_;
    std::deque<RedisConnection> pool_;                     // 空闲连接（连接对象本身）
    std::mutex mutex_;
    std::condition_variable cv_;
    // 服务可达性：由业务操作结果（set/get 失败）、create、health_check 维护
    std::atomic_bool up_{false};
    std::atomic_bool stop_{false};
    std::thread health_thread_;
};

}  // namespace common
