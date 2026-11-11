package com.example.smartcampus.network.protocol

/**
 * 消息类型字段表，与服务端 `common/include/protocol.h` 的 `MsgType` **严格对齐**。
 *
 * - 0x0001/0x0002（原 register/login）已迁移到 HTTP AuthServer，保留不用；
 * - 响应消息最高位（0x8000）为 1，见 [isResponse]。
 */
object MsgType {

    // —— 请求 / 通知（客户端 → 服务端）——
    const val HEARTBEAT_REQ = 0x0003  // 心跳
    const val LOGOUT_REQ = 0x0004     // 登出
    const val AUTH_REQ = 0x0006       // 首包鉴权：携带 JWT token

    // —— 响应 / 推送（服务端 → 客户端）——
    const val HEARTBEAT_RESP = 0x8003
    const val LOGOUT_RESP = 0x8004
    const val KICK_NOTIFY = 0x8005    // 服务端推送：已在别处登录 / 心跳超时
    const val AUTH_RESP = 0x8006
    const val ERROR = 0x8000          // 通用错误

    /** 0x8000 位为响应标志 */
    fun isResponse(msgType: Int): Boolean = (msgType and 0x8000) != 0
}
