// AOI 空间索引方案压测对比
// 四种候选：十字链表 / 四叉树 / 固定九宫格 / 动态自适应九宫格
//
// 编译（建议开优化，否则耗时数据失真）：
//   g++ -O2 -std=c++20 aoi_benchmark.cpp -o aoi_benchmark
//   MSVC: cl /EHsc /std:c++20 /O2 aoi_benchmark.cpp
//
// 说明：这是独立的选型原型，不依赖网关的 PG/Redis/gRPC，纯内存模拟。
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <windows.h>

// ============================ 参数 ============================
constexpr double MAP_SIZE = 2000.0;   // 地图边长（米）
constexpr double CELL_SIZE = 100.0;   // 基础网格边长（米）
constexpr double AOI_RADIUS = 150.0;  // AOI 半径（米），即 300m×300m 邻域
constexpr int N_USERS = 10000;        // 用户数
constexpr int M_MOVES = 200000;       // 移动 + 查询 操作次数

struct Pos {
    double x;
    double y;
};

// ============================ 统一接口 ============================
struct AoiScheme {
    virtual ~AoiScheme() = default;
    virtual void insert(uint32_t uid, Pos p) = 0;
    virtual void move(uint32_t uid, Pos p) = 0;
    virtual void remove(uint32_t uid) = 0;
    virtual std::vector<uint32_t> query_near(uint32_t uid) const = 0;
    virtual std::string name() const = 0;
};

// ============================ 1. 十字链表 ============================
// X/Y 双排序结构，范围查询 = 遍历 X 区间 + 按 Y 过滤（O(条带宽度)）
class CrossList : public AoiScheme {
public:
    void insert(uint32_t uid, Pos p) override {
        pos_[uid] = p;
        by_x_[p.x].insert(uid);
        by_y_[p.y].insert(uid);
    }
    void move(uint32_t uid, Pos p) override {
        remove(uid);
        insert(uid, p);
    }
    void remove(uint32_t uid) override {
        auto it = pos_.find(uid);
        if (it == pos_.end()) return;
        Pos p = it->second;
        // 用 find 而非 operator[]：operator[] 会在键不存在时插入空 set，
        // 导致 map 无限膨胀、查询遍历大量空键（曾使压测跑不完）
        auto xit = by_x_.find(p.x);
        if (xit != by_x_.end()) {
            xit->second.erase(uid);
            if (xit->second.empty()) by_x_.erase(xit);
        }
        auto yit = by_y_.find(p.y);
        if (yit != by_y_.end()) {
            yit->second.erase(uid);
            if (yit->second.empty()) by_y_.erase(yit);
        }
        pos_.erase(it);
    }
    std::vector<uint32_t> query_near(uint32_t uid) const override {
        Pos p = pos_.at(uid);
        std::vector<uint32_t> res;
        auto lo = by_x_.lower_bound(p.x - AOI_RADIUS);
        auto hi = by_x_.upper_bound(p.x + AOI_RADIUS);
        for (auto it = lo; it != hi; ++it) {
            for (uint32_t u : it->second) {
                Pos q = pos_.at(u);
                if (u != uid && q.y >= p.y - AOI_RADIUS && q.y <= p.y + AOI_RADIUS) {
                    res.push_back(u);
                }
            }
        }
        return res;
    }
    std::string name() const override { return "十字链表"; }

private:
    std::map<double, std::unordered_set<uint32_t>> by_x_;
    std::map<double, std::unordered_set<uint32_t>> by_y_;
    std::unordered_map<uint32_t, Pos> pos_;
};

// ============================ 2. 四叉树 ============================
class Quadtree : public AoiScheme {
    struct Node {
        double x0, y0, x1, y1;
        std::vector<std::pair<uint32_t, Pos>> pts;
        std::array<std::unique_ptr<Node>, 4> child;
        bool leaf = true;
    };
    static constexpr int CAP = 16;  // 叶子容量，超过则分裂

public:
    Quadtree() { root_ = std::make_unique<Node>(Node{0, 0, MAP_SIZE, MAP_SIZE}); }

    void insert(uint32_t uid, Pos p) override {
        pos_[uid] = p;
        Node* n = find_leaf(root_.get(), p);
        n->pts.push_back({uid, p});
        where_[uid] = n;
        if (n->leaf && static_cast<int>(n->pts.size()) > CAP) split(n);
    }
    void move(uint32_t uid, Pos p) override {
        remove(uid);
        insert(uid, p);
    }
    void remove(uint32_t uid) override {
        auto it = where_.find(uid);
        if (it == where_.end()) return;
        Node* n = it->second;
        n->pts.erase(std::remove_if(n->pts.begin(), n->pts.end(),
                                    [uid](const auto& e) { return e.first == uid; }),
                     n->pts.end());
        where_.erase(it);
        pos_.erase(uid);
        // 简化：不合并节点
    }
    std::vector<uint32_t> query_near(uint32_t uid) const override {
        Pos p = pos_.at(uid);
        std::vector<uint32_t> res;
        query_rec(root_.get(), p, uid, res);
        return res;
    }
    std::string name() const override { return "四叉树"; }

private:
    Node* child_of(Node* n, Pos p) const {
        double mx = (n->x0 + n->x1) / 2, my = (n->y0 + n->y1) / 2;
        int idx = (p.x >= mx ? 1 : 0) + (p.y >= my ? 2 : 0);
        return n->child[idx].get();
    }
    Node* find_leaf(Node* n, Pos p) {
        while (!n->leaf) n = child_of(n, p);
        return n;
    }
    void split(Node* n) {
        double mx = (n->x0 + n->x1) / 2, my = (n->y0 + n->y1) / 2;
        n->child[0] = std::make_unique<Node>(Node{n->x0, n->y0, mx, my});
        n->child[1] = std::make_unique<Node>(Node{mx, n->y0, n->x1, my});
        n->child[2] = std::make_unique<Node>(Node{n->x0, my, mx, n->y1});
        n->child[3] = std::make_unique<Node>(Node{mx, my, n->x1, n->y1});
        n->leaf = false;
        for (auto& [uid, p] : n->pts) {
            Node* c = child_of(n, p);
            c->pts.push_back({uid, p});
            where_[uid] = c;
        }
        n->pts.clear();
    }
    void query_rec(Node* n, Pos p, uint32_t self, std::vector<uint32_t>& res) const {
        if (!n || n->x1 < p.x - AOI_RADIUS || n->x0 > p.x + AOI_RADIUS ||
            n->y1 < p.y - AOI_RADIUS || n->y0 > p.y + AOI_RADIUS) return;
        if (n->leaf) {
            for (auto& [uid, q] : n->pts) {
                if (uid != self && q.x >= p.x - AOI_RADIUS && q.x <= p.x + AOI_RADIUS &&
                    q.y >= p.y - AOI_RADIUS && q.y <= p.y + AOI_RADIUS) {
                    res.push_back(uid);
                }
            }
        } else {
            for (auto& c : n->child) query_rec(c.get(), p, self, res);
        }
    }

    std::unique_ptr<Node> root_;
    std::unordered_map<uint32_t, Node*> where_;
    std::unordered_map<uint32_t, Pos> pos_;
};

// ============================ 3. 固定九宫格 ============================
class FixedGrid : public AoiScheme {
public:
    void insert(uint32_t uid, Pos p) override {
        std::string k = key(p);
        grid_[k].insert(uid);
        uid_grid_[uid] = k;
        pos_[uid] = p;
    }
    void move(uint32_t uid, Pos p) override {
        remove(uid);
        insert(uid, p);
    }
    void remove(uint32_t uid) override {
        auto it = uid_grid_.find(uid);
        if (it == uid_grid_.end()) return;
        grid_[it->second].erase(uid);
        uid_grid_.erase(it);
        pos_.erase(uid);
    }
    std::vector<uint32_t> query_near(uint32_t uid) const override {
        Pos p = pos_.at(uid);
        int gx = static_cast<int>(std::floor(p.x / CELL_SIZE));
        int gy = static_cast<int>(std::floor(p.y / CELL_SIZE));
        std::vector<uint32_t> res;
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                auto it = grid_.find(key_of(gx + dx, gy + dy));
                if (it == grid_.end()) continue;
                for (uint32_t u : it->second) {
                    if (u != uid) res.push_back(u);
                }
            }
        }
        return res;
    }
    std::string name() const override { return "固定九宫格"; }

private:
    std::string key(Pos p) const {
        return key_of(static_cast<int>(std::floor(p.x / CELL_SIZE)),
                      static_cast<int>(std::floor(p.y / CELL_SIZE)));
    }
    std::string key_of(int gx, int gy) const {
        return std::to_string(gx) + "," + std::to_string(gy);
    }
    std::unordered_map<std::string, std::unordered_set<uint32_t>> grid_;
    std::unordered_map<uint32_t, std::string> uid_grid_;
    std::unordered_map<uint32_t, Pos> pos_;
};

// ============================ 4. 动态自适应九宫格 ============================
// 在固定九宫格基础上，热点格（人数 > 阈值）切分为更细子格，减少过度广播。
class AdaptiveGrid : public AoiScheme {
    static constexpr size_t SPLIT_TH = 200;            // 人数阈值
    static constexpr double FINE = CELL_SIZE / 2.0;    // 切分后 50m

public:
    void insert(uint32_t uid, Pos p) override {
        pos_[uid] = p;
        std::string base = base_key(p);
        ++base_count_[base];
        std::string k = base_count_[base] > SPLIT_TH ? fine_key(p) : base;
        grid_[k].insert(uid);
        uid_grid_[uid] = k;
    }
    void move(uint32_t uid, Pos p) override {
        remove(uid);
        insert(uid, p);
    }
    void remove(uint32_t uid) override {
        auto it = uid_grid_.find(uid);
        if (it == uid_grid_.end()) return;
        grid_[it->second].erase(uid);
        uid_grid_.erase(it);
        pos_.erase(uid);
        // 简化：不统计合并（只演示切分）
    }
    std::vector<uint32_t> query_near(uint32_t uid) const override {
        Pos p = pos_.at(uid);
        int gx = static_cast<int>(std::floor(p.x / CELL_SIZE));
        int gy = static_cast<int>(std::floor(p.y / CELL_SIZE));
        std::vector<uint32_t> res;
        // 遍历 9 个基础格；每格若被切分，则遍历其 4 个 50m 子格（过滤距离）
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                std::string base = base_key_of(gx + dx, gy + dy);
                // 检查该基础格是否被切分（存在 fine 子格键）
                bool split = false;
                for (int fx = 0; fx < 2; ++fx) {
                    for (int fy = 0; fy < 2; ++fy) {
                        auto it = grid_.find(fine_key_of(gx + dx, gy + dy, fx, fy));
                        if (it == grid_.end()) continue;
                        split = true;
                        for (uint32_t u : it->second) {
                            Pos q = pos_.at(u);
                            if (u != uid && q.x >= p.x - AOI_RADIUS && q.x <= p.x + AOI_RADIUS &&
                                q.y >= p.y - AOI_RADIUS && q.y <= p.y + AOI_RADIUS) {
                                res.push_back(u);
                            }
                        }
                    }
                }
                if (!split) {
                    auto it = grid_.find(base);
                    if (it == grid_.end()) continue;
                    for (uint32_t u : it->second) {
                        if (u != uid) res.push_back(u);
                    }
                }
            }
        }
        return res;
    }
    std::string name() const override { return "动态自适应九宫格"; }

private:
    std::string base_key(Pos p) const {
        return base_key_of(static_cast<int>(std::floor(p.x / CELL_SIZE)),
                           static_cast<int>(std::floor(p.y / CELL_SIZE)));
    }
    std::string fine_key(Pos p) const {
        int gx = static_cast<int>(std::floor(p.x / CELL_SIZE));
        int gy = static_cast<int>(std::floor(p.y / CELL_SIZE));
        int fx = static_cast<int>(std::floor((p.x - gx * CELL_SIZE) / FINE));
        int fy = static_cast<int>(std::floor((p.y - gy * CELL_SIZE) / FINE));
        return fine_key_of(gx, gy, fx, fy);
    }
    std::string base_key_of(int gx, int gy) const {
        return "B" + std::to_string(gx) + "," + std::to_string(gy);
    }
    std::string fine_key_of(int gx, int gy, int fx, int fy) const {
        return "F" + std::to_string(gx) + "," + std::to_string(gy) + "," +
               std::to_string(fx) + "," + std::to_string(fy);
    }
    std::unordered_map<std::string, std::unordered_set<uint32_t>> grid_;
    std::unordered_map<uint32_t, std::string> uid_grid_;
    std::unordered_map<uint32_t, Pos> pos_;
    std::unordered_map<std::string, size_t> base_count_;
};

// ============================ 基准 ============================
using Clock = std::chrono::high_resolution_clock;

struct Result {
    double avg_query_us = 0;
    double p99_query_us = 0;
    double avg_update_us = 0;
    double avg_neighbors = 0;
};

double us(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration_cast<std::chrono::microseconds>(b - a).count();
}

Result run_benchmark(AoiScheme& scheme,
                     const std::vector<std::pair<uint32_t, Pos>>& users,
                     const std::vector<std::pair<uint32_t, Pos>>& moves) {
    for (auto& [uid, p] : users) scheme.insert(uid, p);

    std::vector<double> qt, ut;
    qt.reserve(moves.size());
    ut.reserve(moves.size());
    double total_nb = 0;

    for (auto& [uid, np] : moves) {
        auto t0 = Clock::now();
        scheme.move(uid, np);
        auto t1 = Clock::now();
        ut.push_back(us(t0, t1));

        auto t2 = Clock::now();
        auto nb = scheme.query_near(uid);
        auto t3 = Clock::now();
        qt.push_back(us(t2, t3));
        total_nb += nb.size();
    }

    std::sort(qt.begin(), qt.end());
    std::sort(ut.begin(), ut.end());
    double avg_q = 0;
    for (double v : qt) avg_q += v;
    avg_q /= qt.size();
    double avg_u = 0;
    for (double v : ut) avg_u += v;
    avg_u /= ut.size();

    Result r;
    r.avg_query_us = avg_q;
    r.p99_query_us = qt[static_cast<size_t>(qt.size() * 0.99)];
    r.avg_update_us = avg_u;
    r.avg_neighbors = total_nb / moves.size();
    return r;
}

// 生成用户：mode = 0 均匀分布；mode = 1 聚集分布（80% 集中到热点区）
std::vector<std::pair<uint32_t, Pos>> gen_users(std::mt19937& rng, int mode) {
    std::vector<std::pair<uint32_t, Pos>> users(N_USERS);
    std::uniform_real_distribution<double> uni(0.0, MAP_SIZE);
    std::uniform_real_distribution<double> hot(0.0, MAP_SIZE * 0.25);
    for (int i = 0; i < N_USERS; ++i) {
        Pos p;
        if (mode == 1 && (i % 5) != 0) {  // 80% 聚集到左下 25% 面积的热点
            p.x = hot(rng);
            p.y = hot(rng);
        } else {
            p.x = uni(rng);
            p.y = uni(rng);
        }
        users[i] = {static_cast<uint32_t>(i + 1), p};
    }
    return users;
}

std::vector<std::pair<uint32_t, Pos>> gen_moves(std::mt19937& rng, int mode) {
    std::vector<std::pair<uint32_t, Pos>> moves(M_MOVES);
    std::uniform_int_distribution<int> uid(1, N_USERS);
    std::uniform_real_distribution<double> uni(0.0, MAP_SIZE);
    std::uniform_real_distribution<double> hot(0.0, MAP_SIZE * 0.25);
    for (int i = 0; i < M_MOVES; ++i) {
        Pos p;
        if (mode == 1 && (i % 5) != 0) {
            p.x = hot(rng);
            p.y = hot(rng);
        } else {
            p.x = uni(rng);
            p.y = uni(rng);
        }
        moves[i] = {static_cast<uint32_t>(uid(rng)), p};
    }
    return moves;
}

int main() {
#ifdef _WIN32
    // 把控制台输入/输出代码页切到 UTF-8，解决 Windows 下中文乱码
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    std::cout << "=== AOI 方案压测对比 ===\n";
    std::cout << "参数: 用户 " << N_USERS << ", 操作 " << M_MOVES
              << ", 地图 " << MAP_SIZE << "m, 网格 " << CELL_SIZE
              << "m, AOI 半径 " << AOI_RADIUS << "m\n\n";

    const char* dist_names[] = {"均匀分布", "聚集分布"};
    for (int mode = 0; mode < 2; ++mode) {
        std::cout << "【" << dist_names[mode] << "】\n";
        std::cout << std::left << std::setw(20) << "方案"
                  << std::setw(14) << "查询avg(us)"
                  << std::setw(14) << "查询P99(us)"
                  << std::setw(14) << "更新avg(us)"
                  << std::setw(14) << "广播范围"
                  << "\n";

        std::mt19937 rng(42);  // 固定种子，可复现
        auto users = gen_users(rng, mode);
        auto moves = gen_moves(rng, mode);

        std::vector<std::unique_ptr<AoiScheme>> schemes;
        schemes.push_back(std::make_unique<CrossList>());
        schemes.push_back(std::make_unique<Quadtree>());
        schemes.push_back(std::make_unique<FixedGrid>());
        schemes.push_back(std::make_unique<AdaptiveGrid>());

        for (auto& s : schemes) {
            Result r = run_benchmark(*s, users, moves);
            std::cout << std::left << std::setw(20) << s->name()
                      << std::setw(14) << std::fixed << std::setprecision(2) << r.avg_query_us
                      << std::setw(14) << r.p99_query_us
                      << std::setw(14) << r.avg_update_us
                      << std::setw(14) << std::setprecision(1) << r.avg_neighbors
                      << "\n";
        }
        std::cout << "\n";
    }
    return 0;
}
