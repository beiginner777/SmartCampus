package com.example.smartcampus.data.dto

import kotlinx.serialization.Serializable

/**
 * M2 AOI 地图相关 JSON DTO（与服务端坐标上报/广播协议对齐，见 README 第六节）。
 */

@Serializable
data class PositionReportReq(
    val lng: Double,
    val lat: Double,
)

@Serializable
data class NearbyUser(
    val uid: Long,
    val lng: Double,
    val lat: Double,
    val nickname: String = "",
)

@Serializable
data class PositionReportResp(
    val code: Int = -1,
    val nearby: List<NearbyUser> = emptyList(),
)

@Serializable
data class UserEnter(
    val uid: Long,
    val lng: Double,
    val lat: Double,
    val nickname: String = "",
)

@Serializable
data class UserLeave(
    val uid: Long,
)

@Serializable
data class PositionUpdate(
    val uid: Long,
    val lng: Double,
    val lat: Double,
)
