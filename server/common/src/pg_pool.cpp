#include "pg_pool.h"

#include <libpq-fe.h>

#include "logger.h"

#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace common {

// ---------------- PgResult ----------------
PgResult::PgResult(PGresult* res) : res_(res) {}
PgResult::~PgResult() {
    if (res_) {
        PQclear(res_);
    }
}
PgResult::PgResult(PgResult&& other) noexcept : res_(other.res_) { other.res_ = nullptr; }
PgResult& PgResult::operator=(PgResult&& other) noexcept {
    if (this != &other) {
        if (res_) {
            PQclear(res_);
        }
        res_ = other.res_;
        other.res_ = nullptr;
    }
    return *this;
}
int PgResult::rows() const { return PQntuples(res_); }
int PgResult::cols() const { return PQnfields(res_); }
const char* PgResult::value(int row, int col) const { return PQgetvalue(res_, row, col); }
bool PgResult::is_null(int row, int col) const { return PQgetisnull(res_, row, col) != 0; }

// ---------------- PgConnection ----------------
PgConnection::PgConnection(PGconn* conn) : conn_(conn) {}
PgConnection::~PgConnection() {
    if (conn_) {
        PQfinish(conn_);
    }
}
bool PgConnection::is_ok() const { return conn_ && PQstatus(conn_) == CONNECTION_OK; }
std::string PgConnection::error() const { return conn_ ? PQerrorMessage(conn_) : "no connection"; }

namespace {
// 参数化查询（文本格式），成功返回 PGresult*（调用方负责 PQclear）
PGresult* run_params(PGconn* conn, const std::string& sql,
                     const std::vector<std::string>& params) {
    std::vector<const char*> values(params.size(), nullptr);
    for (std::size_t i = 0; i < params.size(); ++i) {
        values[i] = params[i].c_str();
    }
    PGresult* res = PQexecParams(conn, sql.c_str(), static_cast<int>(params.size()), nullptr,
                                 values.data(), nullptr, nullptr, 0);
    if (!res) {
        throw std::runtime_error(std::string("PQexecParams 失败: ") + PQerrorMessage(conn));
    }
    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
        std::string msg = PQresultErrorMessage(res);
        PQclear(res);
        throw std::runtime_error("SQL 执行失败: " + msg);
    }
    return res;
}
}  // namespace

int PgConnection::execute(const std::string& sql, const std::vector<std::string>& params) {
    PGresult* res = run_params(conn_, sql, params);
    const char* n = PQcmdTuples(res);
    int affected = (n && *n) ? std::atoi(n) : 0;
    PQclear(res);
    return affected;
}

PgResult PgConnection::query(const std::string& sql, const std::vector<std::string>& params) {
    return PgResult(run_params(conn_, sql, params));
}

bool PgConnection::ping() {
    if (!conn_ || PQstatus(conn_) != CONNECTION_OK) {
        return false;
    }
    PGresult* res = PQexec(conn_, "SELECT 1");
    bool ok = (res && PQresultStatus(res) == PGRES_TUPLES_OK);
    if (res) {
        PQclear(res);
    }
    return ok;
}

// ---------------- PooledPgConnection ----------------
PooledPgConnection::PooledPgConnection(std::shared_ptr<PgConnection> conn, PgPool* pool)
    : conn_(std::move(conn)), pool_(pool) {}
PooledPgConnection::~PooledPgConnection() {
    if (conn_) {
        pool_->release(std::move(conn_));
    }
}
PooledPgConnection::PooledPgConnection(PooledPgConnection&& o) noexcept
    : conn_(std::move(o.conn_)), pool_(o.pool_) {}
PooledPgConnection& PooledPgConnection::operator=(PooledPgConnection&& o) noexcept {
    if (this != &o) {
        if (conn_) {
            pool_->release(std::move(conn_));
        }
        conn_ = std::move(o.conn_);
        pool_ = o.pool_;
    }
    return *this;
}

// ---------------- PgPool ----------------
PgPool::PgPool(Options opt) : opt_(std::move(opt)) {
    for (int i = 0; i < opt_.pool_size; ++i) {
        auto conn = create();
        if (conn) {
            idle_.push_back(std::move(conn));
        }
    }
    available_ = !idle_.empty();
    if (available_) {
        LOG_INFO("PostgreSQL 连接池就绪：{}/{} 连接", idle_.size(), opt_.pool_size);
    } else {
        LOG_WARN("PostgreSQL 连接池初始化失败，{} 个连接全部不可用", opt_.pool_size);
    }
    health_thread_ = std::thread(&PgPool::health_check_loop, this);
}

PgPool::~PgPool() {
    stop_ = true;
    cv_.notify_all();
    if (health_thread_.joinable()) {
        health_thread_.join();
    }
    // idle_ 中的 shared_ptr 析构时自动 PQfinish
}

PooledPgConnection PgPool::acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !idle_.empty() || !available_.load(); });
    if (idle_.empty()) {
        throw std::runtime_error("PostgreSQL 连接池不可用");
    }
    auto conn = std::move(idle_.back());
    idle_.pop_back();
    return PooledPgConnection(std::move(conn), this);
}

void PgPool::release(std::shared_ptr<PgConnection> conn) {
    if (!conn) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    idle_.push_back(std::move(conn));
    cv_.notify_one();
}

std::shared_ptr<PgConnection> PgPool::create() {
    std::string conninfo = "host=" + opt_.host + " port=" + std::to_string(opt_.port) +
                           " user=" + opt_.user + " password=" + opt_.password +
                           " dbname=" + opt_.database;
    PGconn* raw = PQconnectdb(conninfo.c_str());
    if (!raw || PQstatus(raw) != CONNECTION_OK) {
        std::string msg = raw ? PQerrorMessage(raw) : "PQconnectdb 返回空";
        if (raw) {
            PQfinish(raw);
        }
        LOG_DEBUG("PostgreSQL 连接失败: {}", msg);
        return nullptr;
    }
    return std::make_shared<PgConnection>(raw);
}

void PgPool::health_check_loop() {
    while (!stop_.load()) {
        // 每 30s 检查一次（按秒睡眠以便及时响应停止）
        for (int i = 0; i < 30 && !stop_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        if (stop_.load()) {
            break;
        }

        // 1) 取出所有空闲连接做 SELECT 1 心跳
        std::vector<std::shared_ptr<PgConnection>> idle;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            idle.swap(idle_);
        }
        for (auto& conn : idle) {
            if (conn->ping()) {
                std::lock_guard<std::mutex> lock(mutex_);
                idle_.push_back(conn);
                cv_.notify_one();
            } else {
                LOG_DEBUG("PostgreSQL 连接失效，释放");
                // conn 析构时自动 PQfinish
            }
        }

        // 2) 补齐到 pool_size
        std::size_t current;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            current = idle_.size();
        }
        for (std::size_t i = current; i < static_cast<std::size_t>(opt_.pool_size); ++i) {
            auto nc = create();
            if (!nc) {
                break;  // PostgreSQL 仍不可用
            }
            std::lock_guard<std::mutex> lock(mutex_);
            idle_.push_back(nc);
            cv_.notify_one();
        }

        // 3) 更新可用状态
        {
            std::lock_guard<std::mutex> lock(mutex_);
            available_ = !idle_.empty();
        }
    }
}

}  // namespace common
