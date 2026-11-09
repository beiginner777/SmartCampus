package com.example.smartcampus.network

/**
 * 客户端连接状态机。
 *
 * M0 只区分「未连接 / 连接中 / 已连接 / 已断开」；
 * M1 起会在此基础上叠加「鉴权中 / 已登录 / 异地互踢下线」等状态。
 */
sealed interface ConnectionState {

    /** 尚未发起连接 */
    data object Idle : ConnectionState

    /** 正在建立 TCP 连接 */
    data object Connecting : ConnectionState

    /** 已连接，可收发数据 */
    data object Connected : ConnectionState

    /** 已断开（主动或被动），[reason] 为断开原因 */
    data class Disconnected(val reason: String) : ConnectionState
}
