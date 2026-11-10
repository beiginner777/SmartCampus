#include "redis_pool.h"

#include <hiredis.h>

#include "logger.h"

#include <chrono>
#include <utility>
#include <vector>

namespace common {

// ---------------- PooledRedisConnection ----------------
PooledRedisConnection::~PooledRedisConnection() {
    if (ctx_) {
        pool_->release(ctx_);
    }
}

PooledRedisConnection::PooledRedisConnection(PooledRedisConnection&& o) noexcept
    : ctx_(o.ctx_), pool_(o.pool_) {
    o.ctx_ = nullptr;
}

PooledRedisConnection& PooledRedisConnection::operator=(PooledRedisConnection&& o) noexcept {
    if (this != &o) {
        if (ctx_) {
            pool_->release(ctx_);
        }
        ctx_ = o.ctx_;
        pool_ = o.pool_;
        o.ctx_ = nullptr;
    }
    return *this;
}

// ---------------- RedisPool ----------------
RedisPool::RedisPool(Options opt) : opt_(std::move(opt)) {
    for (int i = 0; i < opt_.pool_size; ++i) {
        redisContext* ctx = create();
        if (ctx) {
            pool_.push(ctx);
        }
    }
    available_ = !pool_.empty();
    if (available_) {
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
    std::lock_guard<std::mutex> lock(mutex_);
    while (!pool_.empty()) {
        redisContext* ctx = pool_.front();
        pool_.pop();
        redisFree(ctx);
    }
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

PooledRedisConnection RedisPool::acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !pool_.empty() || !available_.load(); });
    if (pool_.empty()) {
        return PooledRedisConnection(nullptr, this);  // Redis 不可用
    }
    redisContext* ctx = pool_.front();
    pool_.pop();
    return PooledRedisConnection(ctx, this);
}

void RedisPool::release(redisContext* ctx) {
    if (!ctx) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    pool_.push(ctx);
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
    if (reply && reply->type == REDIS_REPLY_STRING) {
        result = std::string(reply->str, reply->len);
    }
    if (reply) {
        freeReplyObject(reply);
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
    return ok;
}

void RedisPool::health_check_loop() {
    while (!stop_.load()) {
        // 每 30s 检查一次（按秒睡眠以便及时响应停止）
        for (int i = 0; i < 30 && !stop_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        if (stop_.load()) {
            break;
        }

        // 1) 取出所有空闲连接做 PING 检查
        std::vector<redisContext*> idle;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            while (!pool_.empty()) {
                idle.push_back(pool_.front());
                pool_.pop();
            }
        }
        for (auto* ctx : idle) {
            redisReply* reply = static_cast<redisReply*>(redisCommand(ctx, "PING"));
            bool ok = (reply && reply->type == REDIS_REPLY_STATUS && ctx->err == 0);
            if (reply) {
                freeReplyObject(reply);
            }
            if (ok) {
                std::lock_guard<std::mutex> lock(mutex_);
                pool_.push(ctx);
                cv_.notify_one();
            } else {
                LOG_DEBUG("Redis 连接失效，释放");
                redisFree(ctx);
            }
        }

        // 2) 补齐到 pool_size（含 Redis 恢复后池空的情况）
        std::size_t current;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            current = pool_.size();
        }
        for (std::size_t i = current; i < static_cast<std::size_t>(opt_.pool_size); ++i) {
            redisContext* nc = create();
            if (!nc) {
                break;  // Redis 仍不可用
            }
            std::lock_guard<std::mutex> lock(mutex_);
            pool_.push(nc);
            cv_.notify_one();
        }

        // 3) 更新可用状态
        {
            std::lock_guard<std::mutex> lock(mutex_);
            available_ = !pool_.empty();
        }
    }
}

}  // namespace common
