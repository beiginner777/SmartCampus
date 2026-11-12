package com.example.smartcampus.network

import com.example.smartcampus.data.dto.ApiResponse
import com.example.smartcampus.data.dto.AuthRequest
import com.example.smartcampus.data.dto.AuthResponse
import com.example.smartcampus.data.dto.KickNotify
import com.example.smartcampus.data.dto.NearbyUser
import com.example.smartcampus.data.dto.PositionReportReq
import com.example.smartcampus.data.dto.PositionReportResp
import com.example.smartcampus.data.dto.PositionUpdate
import com.example.smartcampus.data.dto.UserEnter
import com.example.smartcampus.data.dto.UserLeave
import com.example.smartcampus.network.protocol.MsgType
import com.example.smartcampus.network.protocol.Packet
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.serialization.decodeFromString
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json

/** 会话层状态机 */
enum class GatewayState {
    Idle,          // 未连接
    Connecting,    // 正在 TCP 连接
    Authing,       // 已连接，正在鉴权（等 AuthResp）
    Authed,        // 已鉴权，正常通信
    Disconnected,  // 已断开（含被踢）
}

/** 会话层事件（一次性，供 UI 弹窗/提示） */
sealed interface GatewayEvent {
    data class Kicked(val reason: String) : GatewayEvent
    data class AuthFailed(val msg: String) : GatewayEvent
}

/**
 * 会话层客户端：在 [TcpClient] 传输层之上，实现 M1 鉴权链路。
 *
 * 职责（镜像服务端 gateway/AuthService）：
 *  - 连上后首包发 `AuthReq{token}`，等待 `AuthResp`（带超时）；
 *  - 鉴权成功后周期性发心跳（30s）；
 *  - 断线自动重连（重发 AuthReq）；
 *  - 收到 `KickNotify` 上抛「已被踢」事件并停止重连。
 */
class GatewayClient(
    private val tcpClient: TcpClient,
    private val json: Json,
    private val scope: CoroutineScope,
) {

    private val _state = MutableStateFlow(GatewayState.Idle)
    val state: StateFlow<GatewayState> = _state.asStateFlow()

    /** 传输层连接状态（区分「连接网关」与「鉴权」两步） */
    val connectionState: StateFlow<ConnectionState> = tcpClient.connectionState

    private val _events = MutableSharedFlow<GatewayEvent>(extraBufferCapacity = 16)
    val events: SharedFlow<GatewayEvent> = _events.asSharedFlow()

    /** 附近在线用户（uid → 用户），由服务端 USER_ENTER/LEAVE/UPDATE + POSITION_REPORT_RESP 增量维护 */
    private val _presence = MutableStateFlow<Map<Long, NearbyUser>>(emptyMap())
    val presence: StateFlow<Map<Long, NearbyUser>> = _presence.asStateFlow()

    private var host = ""
    private var port = 0
    private var token = ""

    /** 是否期望保持在线（主动登出/被踢时置 false，停止重连） */
    private var wantReconnect = false

    private var heartbeatJob: Job? = null
    private var authTimeoutJob: Job? = null

    init {
        observeTransport()
    }

    /** 连接并鉴权；[token] 为登录拿到的 JWT */
    fun connect(host: String, port: Int, token: String) {
        this.host = host
        this.port = port
        this.token = token
        wantReconnect = true
        _state.value = GatewayState.Connecting
        tcpClient.connect(host, port)
    }

    /** 登出：发 LogoutReq 后断开，不再重连 */
    fun logout() {
        wantReconnect = false
        tcpClient.send(MsgType.LOGOUT_REQ, byteArrayOf())
        stopHeartbeat()
        tcpClient.disconnect()
        _state.value = GatewayState.Idle
    }

    /** 主动断开（不登出） */
    fun disconnect() {
        wantReconnect = false
        stopHeartbeat()
        tcpClient.disconnect()
        _state.value = GatewayState.Idle
    }

    /** 上报坐标（仅鉴权成功后才发） */
    fun sendPosition(lng: Double, lat: Double) {
        if (_state.value != GatewayState.Authed) return
        val body = json.encodeToString(PositionReportReq(lng, lat)).toByteArray(Charsets.UTF_8)
        tcpClient.send(MsgType.POSITION_REPORT_REQ, body)
    }

    private fun observeTransport() {
        scope.launch {
            tcpClient.connectionState.collect { st ->
                when (st) {
                    is ConnectionState.Connected -> onConnected()
                    is ConnectionState.Disconnected -> onDisconnected(st.reason)
                    else -> Unit
                }
            }
        }
        scope.launch {
            tcpClient.packets.collect { pkt -> handlePacket(pkt) }
        }
    }

    private fun onConnected() {
        _state.value = GatewayState.Authing
        val body = json.encodeToString(AuthRequest(token)).toByteArray(Charsets.UTF_8)
        tcpClient.send(MsgType.AUTH_REQ, body)

        // 鉴权超时：连上后迟迟收不到 AuthResp 则断开重试
        authTimeoutJob?.cancel()
        authTimeoutJob = scope.launch {
            delay(AUTH_TIMEOUT_MS)
            if (_state.value == GatewayState.Authing) {
                _events.tryEmit(GatewayEvent.AuthFailed("鉴权超时"))
                tcpClient.disconnect()
            }
        }
    }

    private fun onDisconnected(reason: String) {
        stopHeartbeat()
        authTimeoutJob?.cancel()
        _presence.value = emptyMap()  // 断线清空附近用户，重连后由 POSITION_REPORT_RESP 重建
        if (_state.value == GatewayState.Idle) return  // 主动断开，无需处理
        _state.value = GatewayState.Disconnected
        if (wantReconnect) {
            scheduleReconnect()
        }
    }

    private fun scheduleReconnect() {
        scope.launch {
            delay(RECONNECT_DELAY_MS)
            if (wantReconnect) {
                _state.value = GatewayState.Connecting
                tcpClient.connect(host, port)
            }
        }
    }

    private fun handlePacket(pkt: Packet) {
        when (pkt.msgType) {
            MsgType.AUTH_RESP -> handleAuthResp(pkt)
            MsgType.HEARTBEAT_RESP -> Unit  // 心跳应答，无需处理
            MsgType.LOGOUT_RESP -> Unit
            MsgType.KICK_NOTIFY -> handleKick(pkt)
            MsgType.ERROR -> handleError(pkt)
            MsgType.POSITION_REPORT_RESP -> handlePositionResp(pkt)
            MsgType.USER_ENTER -> handleUserEnter(pkt)
            MsgType.USER_LEAVE -> handleUserLeave(pkt)
            MsgType.POSITION_UPDATE -> handlePositionUpdate(pkt)
            else -> Unit
        }
    }

    private fun handleAuthResp(pkt: Packet) {
        authTimeoutJob?.cancel()
        val resp = decodeOrNull<AuthResponse>(pkt.body)
        if (resp?.code == 0) {
            _state.value = GatewayState.Authed
            startHeartbeat()
        } else {
            _state.value = GatewayState.Disconnected
            wantReconnect = false
            _events.tryEmit(GatewayEvent.AuthFailed(resp?.msg ?: "鉴权失败"))
            tcpClient.disconnect()
        }
    }

    private fun handleKick(pkt: Packet) {
        val kick = decodeOrNull<KickNotify>(pkt.body)
        val reason = kick?.reason ?: "已在别处登录"
        // 「心跳超时」是切后台/网络抖动导致的可恢复断开 → 保留自动重连；
        // 「已在别处登录」才是真互踢 → 停止重连并弹窗。
        val recoverable = reason.contains("心跳超时")
        stopHeartbeat()
        authTimeoutJob?.cancel()
        wantReconnect = recoverable
        if (!recoverable) {
            _events.tryEmit(GatewayEvent.Kicked(reason))
        }
        // 关闭连接，交由 onDisconnected 依据 wantReconnect 决定是否重连
        tcpClient.disconnect()
    }

    private fun handleError(pkt: Packet) {
        val err = decodeOrNull<ApiResponse>(pkt.body)
        // 鉴权阶段的 Error（如 token 失效）按鉴权失败处理
        if (_state.value == GatewayState.Authing) {
            authTimeoutJob?.cancel()
            _state.value = GatewayState.Disconnected
            wantReconnect = false
            _events.tryEmit(GatewayEvent.AuthFailed(err?.msg ?: "未知错误"))
            tcpClient.disconnect()
        }
    }

    private fun handlePositionResp(pkt: Packet) {
        val resp = decodeOrNull<PositionReportResp>(pkt.body) ?: return
        if (resp.code != 0) return
        _presence.value = resp.nearby.associateBy { it.uid }
    }

    private fun handleUserEnter(pkt: Packet) {
        val u = decodeOrNull<UserEnter>(pkt.body) ?: return
        _presence.update { it + (u.uid to NearbyUser(u.uid, u.lng, u.lat, u.nickname)) }
    }

    private fun handleUserLeave(pkt: Packet) {
        val u = decodeOrNull<UserLeave>(pkt.body) ?: return
        _presence.update { it - u.uid }
    }

    private fun handlePositionUpdate(pkt: Packet) {
        val u = decodeOrNull<PositionUpdate>(pkt.body) ?: return
        _presence.update { map ->
            val existing = map[u.uid] ?: return@update map
            map + (u.uid to existing.copy(lng = u.lng, lat = u.lat))
        }
    }

    private fun startHeartbeat() {
        heartbeatJob?.cancel()
        heartbeatJob = scope.launch {
            while (true) {
                delay(HEARTBEAT_INTERVAL_MS)
                tcpClient.send(MsgType.HEARTBEAT_REQ, byteArrayOf())
            }
        }
    }

    private fun stopHeartbeat() {
        heartbeatJob?.cancel()
        heartbeatJob = null
    }

    private inline fun <reified T> decodeOrNull(body: ByteArray): T? = try {
        json.decodeFromString<T>(body.decodeToString())
    } catch (e: Exception) {
        null
    }

    companion object {
        private const val HEARTBEAT_INTERVAL_MS = 30_000L
        private const val AUTH_TIMEOUT_MS = 10_000L
        private const val RECONNECT_DELAY_MS = 3_000L
    }
}
