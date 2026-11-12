#pragma once

#include <cmath>
#include <cstdint>
#include <utility>

namespace common {

// ============================ 经纬度 → 网格映射（AOI 九宫格 / 边界粗判 / 找搭子 共用）============================
// 校园尺度（几公里）下，经纬度网格近似正方形，度制足够且无需投影转换。
constexpr double kCellSizeDeg = 0.001;  // 约 111m（纬向），对应 ~100m 网格
constexpr double kPi = 3.14159265358979323846;

// 打包：gx << 32 | (uint32_t)gy，支持负坐标、无碰撞
inline int64_t grid_key(double lat, double lng) {
    int64_t gx = static_cast<int64_t>(std::floor(lng / kCellSizeDeg));
    int64_t gy = static_cast<int64_t>(std::floor(lat / kCellSizeDeg));
    return (gx << 32) | (static_cast<uint32_t>(gy));
}

// 解包为 (gx, gy)
inline std::pair<int64_t, int64_t> grid_xy(int64_t key) {
    int64_t gx = key >> 32;
    int64_t gy = static_cast<int32_t>(key & 0xFFFFFFFFLL);
    return {gx, gy};
}

// 由网格索引还原网格 key（供邻格计算）
inline int64_t grid_key_from_xy(int64_t gx, int64_t gy) {
    return (gx << 32) | (static_cast<uint32_t>(gy));
}

// Haversine 大圆距离（米）—— 找搭子 / 帖子"附近 500 米"距离过滤复用
inline double haversine(double lat1, double lng1, double lat2, double lng2) {
    constexpr double kEarthRadius = 6371000.0;  // 地球半径（米）
    double dlat = (lat2 - lat1) * kPi / 180.0;
    double dlng = (lng2 - lng1) * kPi / 180.0;
    double a = std::sin(dlat / 2) * std::sin(dlat / 2) +
               std::cos(lat1 * kPi / 180.0) * std::cos(lat2 * kPi / 180.0) *
                   std::sin(dlng / 2) * std::sin(dlng / 2);
    return 2 * kEarthRadius * std::asin(std::sqrt(a));
}

}  // namespace common
