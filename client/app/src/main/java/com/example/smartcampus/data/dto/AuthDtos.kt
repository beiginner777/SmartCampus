package com.example.smartcampus.data.dto

import kotlinx.serialization.Serializable

/**
 * 鉴权相关 JSON DTO（HTTP AuthServer 请求/响应 + TCP 协议 body）。
 *
 * 字段与服务端 `server/auth/src/auth_handler.cpp`、`gateway/src/auth_service.cpp` 对齐：
 * 错误码约定：0 成功；1 参数错；2 用户名已存在；3 用户名或密码错误；5 库错；6 token 无效/过期。
 */

@Serializable
data class RegisterRequest(
    val username: String,
    val password: String,
    val role: Int = 0,      // 0=学生 1=教师（见 README 角色约定）
    val nickname: String = "",
    val tags: String = "",
)

@Serializable
data class LoginRequest(
    val username: String,
    val password: String,
)

@Serializable
data class RefreshRequest(val token: String)

/** 首包鉴权（TCP AuthReq body） */
@Serializable
data class AuthRequest(val token: String)

/** 通用响应（register 等仅回 code/msg） */
@Serializable
data class ApiResponse(
    val code: Int = -1,
    val msg: String = "",
)

/** 登录响应：成功带 token 与用户信息 */
@Serializable
data class LoginResponse(
    val code: Int = -1,
    val msg: String = "",
    val token: String = "",
    val uid: Long = 0,
    val username: String = "",
    val nickname: String = "",
    val role: Int = 0,
    val tags: String = "",
)

/** refresh 响应 */
@Serializable
data class RefreshResponse(
    val code: Int = -1,
    val msg: String = "",
    val token: String = "",
)

/** TCP AuthResp body */
@Serializable
data class AuthResponse(
    val code: Int = -1,
    val msg: String = "",
    val uid: Long = 0,
    val username: String = "",
)

/** KickNotify body（服务端推送：已在别处登录 / 心跳超时） */
@Serializable
data class KickNotify(
    val reason: String = "",
)
