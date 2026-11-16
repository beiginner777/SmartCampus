#include "pg_pool.h"

#include <libpq-fe.h>

#include "logger.h"

#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <utility>
#include <Exception>

namespace {
constexpr auto kIdleCheckInterval = std::chrono::seconds(30);  // 空闲超过 30s 才做健康检查
}  // namespace

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
PgConnection::PgConnection(PGconn* conn, PgPool* pool,
                           std::chrono::steady_clock::time_point last_used)
    : conn_(conn), pool_(pool), last_used_(last_used) {}

PgConnection::~PgConnection() {
    release_own();
}

PgConnection::PgConnection(PgConnection&& o) noexcept
    : conn_(o.conn_), pool_(o.pool_), last_used_(o.last_used_) {
    o.conn_ = nullptr;
}

PgConnection& PgConnection::operator=(PgConnection&& o) noexcept {
    if (this != &o) {
        release_own();
        conn_ = o.conn_;
        pool_ = o.pool_;
        last_used_ = o.last_used_;
        o.conn_ = nullptr;
    }
    return *this;
}

void PgConnection::release_own() {
    if (!conn_) {
        return;
    }
    if (PQstatus(conn_) == CONNECTION_OK) {
        pool_->release(conn_, last_used_);  // 连接仍健康：归还
    } else {
        PQfinish(conn_);                    // 连接已失效：释放
    }
    conn_ = nullptr;
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
        // 附上 SQLSTATE 码（locale 无关，便于上层按错误类型分支）
        const char* sqlstate = PQresultErrorField(res, PG_DIAG_SQLSTATE);
        if (sqlstate) {
            msg = "SQLSTATE " + std::string(sqlstate) + ": " + msg;
        }
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
    touch();  // 执行成功后刷新最后活跃时间
    return affected;
}

PgResult PgConnection::query(const std::string& sql, const std::vector<std::string>& params) {
    PgResult res(run_params(conn_, sql, params));
    touch();  // 查询成功后刷新最后活跃时间
    return res;
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
    if (ok) {
        touch();
    }
    return ok;
}

// ---------------- PgPool ----------------
PgPool::PgPool(Options opt) : opt_(std::move(opt)) {
    for (int i = 0; i < opt_.pool_size; ++i) {
        PGconn* conn = create();
        if (conn) {
            idle_.emplace_back(conn, this, std::chrono::steady_clock::now());
        }
    }
    up_ = !idle_.empty();
    if (up_) {
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
    // idle_ 成员析构时，每个 PgConnection 析构 → release 里检测 stop_ → 直接 PQfinish
}

PGconn* PgPool::create() {
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
    return raw;
}

PgConnection PgPool::acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !idle_.empty() || !up_.load(); });
    if (idle_.empty()) {
        throw std::runtime_error("PostgreSQL 连接池不可用");
    }
    PgConnection conn = std::move(idle_.front());
    idle_.pop_front();
    return conn;
}

void PgPool::release(PGconn* conn, std::chrono::steady_clock::time_point last_used) {
    if (!conn) {
        return;
    }
    if (stop_) {
        PQfinish(conn);  // 池子正在销毁，直接释放
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    idle_.emplace_back(conn, this, last_used);
    cv_.notify_one();
}

void PgPool::health_check_loop() {
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
        std::vector<PgConnection> stale;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto it = idle_.begin(); it != idle_.end();) {
                if (now - it->last_used() >= kIdleCheckInterval) {
                    stale.push_back(std::move(*it));
                    it = idle_.erase(it);
                } else {
                    ++it;
                }
            }
        }
        // 2) SELECT 1：健康的 ping 内部 touch（析构归还），失效的析构时 PQfinish
        bool any_ok = false;
        for (auto& conn : stale) {
            if (conn.ping()) {
                any_ok = true;
            } else {
                any_ok = false;
            }
        }
        // 只有本轮实际检查了连接，才用结果更新可达性；无检查则保持原值
        if (!stale.empty()) {
            up_ = any_ok;
        }

        // 3) 补齐到 pool_size
        std::size_t current;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            current = idle_.size();
        }
        bool refilled = false;
        for (std::size_t i = current; i < static_cast<std::size_t>(opt_.pool_size); ++i) {
            PGconn* nc = create();
            if (!nc) {
                refilled = false;
                break;  // PostgreSQL 仍不可用
            }
            std::lock_guard<std::mutex> lock(mutex_);
            idle_.emplace_back(nc, this, std::chrono::steady_clock::now());
            cv_.notify_one();
            refilled = true;
        }
        if (refilled) {
            up_ = true;  // 建连成功说明 PostgreSQL 已恢复
        }
        // stale 析构：健康连接归还 idle_，失效连接 PQfinish
    }
}

}  // namespace common
