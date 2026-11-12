#pragma once

#include "geo_utils.h"

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace gateway {

// 邻格查询结果：邻居 uid + 坐标
struct Neighbor {
    uint32_t uid;
    double lat;
    double lng;
};

// 固定九宫格空间索引（M2 AOI）
// 线程安全：单把 std::mutex 串行化（操作均为微秒级；M5 压测后再考虑分段锁优化）。
class AoiIndex {
public:
    // 首次进入：纳入网格，返回新 9 格内其他用户（用于 POSITION_REPORT_RESP.nearby + USER_ENTER 广播）
    std::vector<Neighbor> enter(uint32_t uid, double lat, double lng);

    struct MoveDiff {
        std::vector<Neighbor> entered;  // 新视野 - 旧视野
        std::vector<Neighbor> left;     // 旧视野 - 新视野
        std::vector<Neighbor> stayed;   // 新旧视野交集
    };
    // 移动：跨格时返回进入/离开/仍在；同格时 entered/left 为空、stayed 为 9 格邻居
    MoveDiff move(uint32_t uid, double lat, double lng);

    // 离开：移出网格，返回旧 9 格内其他用户（用于 USER_LEAVE 广播）
    std::vector<Neighbor> leave(uint32_t uid);

    // 查询附近（不改变状态）
    std::vector<Neighbor> query_near(uint32_t uid) const;

    bool contains(uint32_t uid) const;

private:
    // 9 格内除 exclude 外的其他用户（含坐标）
    std::vector<Neighbor> neighbors9(int64_t cell, uint32_t exclude) const;
    // 9 格内除 exclude 外的其他 uid（不含坐标，用于集合求交/差）
    std::unordered_set<uint32_t> neighbors9_uid(int64_t cell, uint32_t exclude) const;
    // 取 uid 的 Neighbor（uid 必然已入索引）
    Neighbor pos_of(uint32_t uid) const;

    mutable std::mutex mutex_;
    std::unordered_map<int64_t, std::unordered_set<uint32_t>> grid_;     // cellKey → uid 集合
    std::unordered_map<uint32_t, int64_t> uid_cell_;                     // uid → cellKey
    std::unordered_map<uint32_t, std::pair<double, double>> uid_pos_;    // uid → (lat, lng)
};

// ============================ 内联实现 ============================

inline std::vector<Neighbor> AoiIndex::enter(uint32_t uid, double lat, double lng) {
    std::lock_guard<std::mutex> lock(mutex_);
    int64_t cell = common::grid_key(lat, lng);
    uid_pos_[uid] = {lat, lng};
    uid_cell_[uid] = cell;
    grid_[cell].insert(uid);
    return neighbors9(cell, uid);
}

inline AoiIndex::MoveDiff AoiIndex::move(uint32_t uid, double lat, double lng) {
    std::lock_guard<std::mutex> lock(mutex_);
    MoveDiff diff;
    int64_t new_cell = common::grid_key(lat, lng);
    auto it = uid_cell_.find(uid);
    if (it == uid_cell_.end()) {
        // 未纳入索引（异常路径），按 enter 处理
        uid_pos_[uid] = {lat, lng};
        uid_cell_[uid] = new_cell;
        grid_[new_cell].insert(uid);
        diff.stayed = neighbors9(new_cell, uid);
        return diff;
    }

    int64_t old_cell = it->second;
    uid_pos_[uid] = {lat, lng};

    if (old_cell == new_cell) {
        // 同格移动：无需改网格，仅刷新坐标；邻居即 stayed
        diff.stayed = neighbors9(new_cell, uid);
        return diff;
    }

    // 跨格：移除旧格、插入新格
    grid_[old_cell].erase(uid);
    grid_[new_cell].insert(uid);
    uid_cell_[uid] = new_cell;

    auto old_nb = neighbors9_uid(old_cell, uid);
    auto new_nb = neighbors9_uid(new_cell, uid);
    for (uint32_t u : new_nb) {
        if (old_nb.count(u)) {
            diff.stayed.push_back(pos_of(u));
        } else {
            diff.entered.push_back(pos_of(u));
        }
    }
    for (uint32_t u : old_nb) {
        if (!new_nb.count(u)) {
            diff.left.push_back(pos_of(u));
        }
    }
    return diff;
}

inline std::vector<Neighbor> AoiIndex::leave(uint32_t uid) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = uid_cell_.find(uid);
    if (it == uid_cell_.end()) {
        return {};
    }
    int64_t cell = it->second;
    grid_[cell].erase(uid);
    uid_cell_.erase(it);
    uid_pos_.erase(uid);
    return neighbors9(cell, uid);
}

inline std::vector<Neighbor> AoiIndex::query_near(uint32_t uid) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = uid_cell_.find(uid);
    if (it == uid_cell_.end()) {
        return {};
    }
    return neighbors9(it->second, uid);
}

inline bool AoiIndex::contains(uint32_t uid) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return uid_cell_.count(uid) != 0;
}

inline std::vector<Neighbor> AoiIndex::neighbors9(int64_t cell, uint32_t exclude) const {
    auto xy = common::grid_xy(cell);
    std::vector<Neighbor> res;
    for (int64_t dx = -1; dx <= 1; ++dx) {
        for (int64_t dy = -1; dy <= 1; ++dy) {
            int64_t key = common::grid_key_from_xy(xy.first + dx, xy.second + dy);
            auto it = grid_.find(key);
            if (it == grid_.end()) continue;
            for (uint32_t u : it->second) {
                if (u == exclude) continue;
                auto p = uid_pos_.find(u);
                if (p == uid_pos_.end()) continue;
                res.push_back({u, p->second.first, p->second.second});
            }
        }
    }
    return res;
}

inline std::unordered_set<uint32_t> AoiIndex::neighbors9_uid(int64_t cell,
                                                             uint32_t exclude) const {
    auto xy = common::grid_xy(cell);
    std::unordered_set<uint32_t> res;
    for (int64_t dx = -1; dx <= 1; ++dx) {
        for (int64_t dy = -1; dy <= 1; ++dy) {
            int64_t key = common::grid_key_from_xy(xy.first + dx, xy.second + dy);
            auto it = grid_.find(key);
            if (it == grid_.end()) continue;
            for (uint32_t u : it->second) {
                if (u != exclude) res.insert(u);
            }
        }
    }
    return res;
}

inline Neighbor AoiIndex::pos_of(uint32_t uid) const {
    auto it = uid_pos_.find(uid);
    if (it == uid_pos_.end()) {
        return {uid, 0.0, 0.0};
    }
    return {uid, it->second.first, it->second.second};
}

}  // namespace gateway
