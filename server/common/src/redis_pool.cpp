#include "redis_pool.h"

#include <hiredis.h>

#include "logger.h"

#include <chrono>
#include <utility>
#include <vector>

namespace {
constexpr auto kIdleCheckInterval = std::chrono::seconds(30);  // 空闲超过 30s 才做健康检查
}  // namespace

namespace common {

// ---------------- RedisConnection ----------------
RedisConnection::RedisConnection(redisContext* ctx, RedisPool* pool,
                                 std::chrono::steady_clock::time_point last_used)
    : ctx_(ctx), pool_(pool), last_used_(last_used) {}

RedisConnection::~RedisConnection() {
    release_own();
}

RedisConnection::RedisConnection(RedisConnection&& o) noexcept
    : ctx_(o.ctx_), pool_(o.pool_), last_used_(o.last_used_), healthy_(o.healthy_) {
    o.ctx_ = nullptr;
}

RedisConnection& RedisConnection::operator=(RedisConnection&& o) noexcept {
    if (this != &o) {
        release_own();
        ctx_ = o.ctx_;
        pool_ = o.pool_;
        last_used_ = o.last_used_;
        healthy_ = o.healthy_;
        o.ctx_ = nullptr;
    }
    return *this;
}

void RedisConnection::release_own() {
    if (!ctx_) {
        return;
    }
    if (healthy_) {
        pool_->release(ctx_, last_used_);  // 归还健康连接
    } else {
        redisFree(ctx_);                   // 失效：直接释放
    }
    ctx_ = nullptr;
}

// ---------------- RedisPool ----------------
RedisPool::RedisPool(Options opt) : opt_(std::move(opt)) {
    for (int i = 0; i < opt_.pool_size; ++i) {
        redisContext* ctx = create();
        if (ctx) {
            pool_.emplace_back(ctx, this, std::chrono::steady_clock::now());
        }
    }
    up_ = !pool_.empty();
    if (up_) {
        LOG_INFO("Redis 连接池就绪：{}/{} 连接", pool_.size(), opt_.pool_size);
    } else {
        LOG_WARN("Redis 连接池初始化失败，{} 个连接全部不可用", opt_.pool_size);
    }
    health_thread_ = std::thread(&RedisPool::health_check_loop, this);
}

RedisPool::~RedisPool() {
    stop_ = true;
    cv_.notify_all();
    if (health_thread_.joinable()) {
        health_thread_.join();
    }
    // pool_ 成员析构时，每个 RedisConnection 析构 → release 里检测 stop_ → 直接 redisFree
}

redisContext* RedisPool::create() {
    redisContext* ctx = redisConnect(opt_.host.c_str(), opt_.port);
    if (!ctx || ctx->err != 0) {
        if (ctx) {
            LOG_DEBUG("连接Redis失败: {}", ctx->errstr);
            redisFree(ctx);
        }
        return nullptr;
    }
    if (!opt_.password.empty()) {
        redisReply* reply =
            static_cast<redisReply*>(redisCommand(ctx, "AUTH %s", opt_.password.c_str()));
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            LOG_DEBUG("Redis 认证失败: {}", reply ? reply->str : "无响应");
            if (reply) {
                freeReplyObject(reply);
            }
            redisFree(ctx);
            return nullptr;
        }
        freeReplyObject(reply);
    }
    return ctx;
}

RedisConnection RedisPool::acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !pool_.empty() || !up_.load(); });
    if (pool_.empty()) {
        return RedisConnection(nullptr, this, std::chrono::steady_clock::now());
    }
    RedisConnection conn = std::move(pool_.front());
    pool_.pop_front();
    return conn;
}

void RedisPool::release(redisContext* ctx, std::chrono::steady_clock::time_point last_used) {
    if (!ctx) {
        return;
    }
    if (stop_) {
        redisFree(ctx);  // 池子正在销毁，直接释放
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    pool_.emplace_back(ctx, this, last_used);
    cv_.notify_one();
}

bool RedisPool::set(const std::string& key, const std::string& value, int ttl_seconds) {
    auto conn = acquire();
    redisContext* ctx = conn.get();
    if (!ctx) {
        return false;
    }
    redisReply* reply;
    if (ttl_seconds > 0) {
        reply = static_cast<redisReply*>(
            redisCommand(ctx, "SET %s %s EX %d", key.c_str(), value.c_str(), ttl_seconds));
    } else {
        reply = static_cast<redisReply*>(
            redisCommand(ctx, "SET %s %s", key.c_str(), value.c_str()));
    }
    bool ok = (reply != nullptr && reply->type != REDIS_REPLY_ERROR);
    if (reply) {
        freeReplyObject(reply);
    }
    if (ok) {
        conn.touch();   // 成功：刷新最后活跃时间
        up_ = true;
    } else {
        conn.mark_bad();  // 失败：析构时释放连接
        up_ = false;
    }
    return ok;
}

std::optional<std::string> RedisPool::get(const std::string& key) {
    auto conn = acquire();
    redisContext* ctx = conn.get();
    if (!ctx) {
        return std::nullopt;
    }
    redisReply* reply = static_cast<redisReply*>(redisCommand(ctx, "GET %s", key.c_str()));
    std::optional<std::string> result;
    bool ok = (reply != nullptr && reply->type != REDIS_REPLY_ERROR);
    if (reply && reply->type == REDIS_REPLY_STRING) {
        result = std::string(reply->str, reply->len);
    }
    if (reply) {
        freeReplyObject(reply);
    }
    if (ok) {
        conn.touch();
        up_ = true;
    } else {
        conn.mark_bad();
        up_ = false;
    }
    return result;
}

bool RedisPool::del(const std::string& key) {
    auto conn = acquire();
    redisContext* ctx = conn.get();
    if (!ctx) {
        return false;
    }
    redisReply* reply = static_cast<redisReply*>(redisCommand(ctx, "DEL %s", key.c_str()));
    bool ok = (reply != nullptr && reply->type != REDIS_REPLY_ERROR);
    if (reply) {
        freeReplyObject(reply);
    }
    if (ok) {
        conn.touch();
        up_ = true;
    } else {
        conn.mark_bad();
        up_ = false;
    }
    return ok;
}

bool RedisPool::publish(const std::string& channel, const std::string& message) {
    auto conn = acquire();
    redisContext* ctx = conn.get();
    if (!ctx) {
        return false;
    }
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx, "PUBLISH %s %s", channel.c_str(), message.c_str()));
    bool ok = (reply != nullptr && reply->type != REDIS_REPLY_ERROR);
    if (reply) {
        freeReplyObject(reply);
    }
    if (ok) {
        conn.touch();
        up_ = true;
    } else {
        conn.mark_bad();
        up_ = false;
    }
    return ok;
}

void RedisPool::subscribe(const std::string& channel,
                          std::function<void(const std::string&)> on_message) {
    // 订阅需要独立连接（SUBSCRIBE 后进入订阅模式，不能再发普通命令），
    // 用后台线程阻塞接收。进程退出时由 OS 回收（M1 不做优雅停止）。
    std::thread([this, channel, on_message = std::move(on_message)]() {
        redisContext* sub = redisConnect(opt_.host.c_str(), opt_.port);
        if (!sub || sub->err) {
            LOG_WARN("Redis 订阅连接失败: {}", sub ? sub->errstr : "null");
            if (sub) {
                redisFree(sub);
            }
            return;
        }
        if (!opt_.password.empty()) {
            redisReply* r = static_cast<redisReply*>(
                redisCommand(sub, "AUTH %s", opt_.password.c_str()));
            if (r) {
                freeReplyObject(r);
            }
        }
        redisReply* r = static_cast<redisReply*>(
            redisCommand(sub, "SUBSCRIBE %s", channel.c_str()));
        if (r) {
            freeReplyObject(r);
        }

        while (!stop_.load()) {
            redisReply* reply = nullptr;
            if (redisGetReply(sub, reinterpret_cast<void**>(&reply)) != REDIS_OK || !reply) {
                break;
            }
            // 消息格式：["message", <channel>, <payload>]
            if (reply->type == REDIS_REPLY_ARRAY && reply->elements >= 3 &&
                reply->element[0]->type == REDIS_REPLY_STRING &&
                std::string(reply->element[0]->str) == "message") {
                on_message(std::string(reply->element[2]->str, reply->element[2]->len));
            }
            freeReplyObject(reply);
        }
        redisFree(sub);
    }).detach();
}

void RedisPool::health_check_loop() {
    while (!stop_.load()) {
        // 每 10s 扫描一次（按秒睡眠以便及时响应停止）
        for (int i = 0; i < 10 && !stop_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        if (stop_.load()) {
            break;
        }

        auto now = std::chrono::steady_clock::now();

        // 1) 移出「空闲超过阈值」的连接（move 到局部 vector，析构时自动归还/释放）
        std::vector<RedisConnection> stale;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto it = pool_.begin(); it != pool_.end();) {
                if (now - it->last_used() >= kIdleCheckInterval) {
                    stale.push_back(std::move(*it));
                    it = pool_.erase(it);
                } else {
                    ++it;
                }
            }
        }
        // 2) PING：健康的 touch（析构归还），失效的 mark_bad（析构释放）
        bool any_ok = false;
        for (auto& conn : stale) {
            redisReply* reply = static_cast<redisReply*>(redisCommand(conn.get(), "PING"));
            bool ok = (reply && reply->type == REDIS_REPLY_STATUS && conn.get()->err == 0);
            if (reply) {
                freeReplyObject(reply);
            }
            if (ok) {
                any_ok = true;
                conn.touch();
            } else {
                any_ok = false;
                conn.mark_bad();
            }
        }
        // 只有本轮实际检查了连接，才用 PING 结果更新可达性；无检查则保持原值
        if (!stale.empty()) {
            up_ = any_ok;
        }

        // 3) 补齐到 pool_size（含 Redis 恢复后池空的情况）
        std::size_t current;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            current = pool_.size();
        }
        bool refilled = false;
        for (std::size_t i = current; i < static_cast<std::size_t>(opt_.pool_size); ++i) {
            redisContext* nc = create();
            if (!nc) {
                refilled = false;
                break;  // Redis 仍不可用
            }
            std::lock_guard<std::mutex> lock(mutex_);
            pool_.emplace_back(nc, this, std::chrono::steady_clock::now());
            cv_.notify_one();
            refilled = true;
        }
        if (refilled) {
            up_ = true;  // 建连成功说明 Redis 已恢复
        }
        // stale 析构：健康连接归还 pool_，失效连接 redisFree
    }
}

}  // namespace common
