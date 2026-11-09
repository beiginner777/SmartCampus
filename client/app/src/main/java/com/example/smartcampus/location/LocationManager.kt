package com.example.smartcampus.location

/**
 * 定位能力抽象（M2 接入）。
 *
 * 职责：权限申请 → 持续定位 → 坐标节流上报（约 3s/次）到服务端 AOI 空间索引。
 * M0 仅占位，不实现；正式实现落高德 location SDK。
 */
interface LocationManager {
    // TODO(M2): 运行时定位权限申请（ACCESS_FINE_LOCATION）
    // TODO(M2): 高德持续定位，暴露 Flow<Location> 回调
    // TODO(M2): 坐标节流上报（对接 network 层，写入服务端 AOI 九宫格索引）
}
