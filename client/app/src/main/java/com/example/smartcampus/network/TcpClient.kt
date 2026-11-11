package com.example.smartcampus.network

import android.util.Log
import com.example.smartcampus.network.protocol.Packet
import com.example.smartcampus.network.protocol.PacketDecoder
import com.example.smartcampus.network.protocol.encodePacket
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import java.io.BufferedInputStream
import java.io.BufferedOutputStream
import java.net.InetSocketAddress
import java.net.Socket
import java.net.SocketException
import java.util.concurrent.atomic.AtomicInteger

/**
 * 原生 TCP 传输层（M1：二进制分帧协议）。
 *
 * 负责「连接管理 + 帧收发 + 粘包/拆包」，**不关心业务语义**：
 *   - 发送：把 (msgType, seq, body) 编码成一帧写出；
 *   - 接收：读循环喂给 [PacketDecoder]，把切出的完整 [Packet] 上抛给上层。
 *
 * 会话/鉴权/心跳/互踢等业务逻辑在上层 [GatewayClient]。
 */
class TcpClient(private val scope: CoroutineScope) {

    /** 连接状态（UI 直接订阅） */
    private val _connectionState = MutableStateFlow<ConnectionState>(ConnectionState.Idle)
    val connectionState: StateFlow<ConnectionState> = _connectionState.asStateFlow()

    /** 收到的完整包（上层据此分发业务） */
    private val _packets = MutableSharedFlow<Packet>(extraBufferCapacity = 64)
    val packets: SharedFlow<Packet> = _packets.asSharedFlow()

    /** 日志流：连接 / 发送 / 断线等事件，供调试展示 */
    private val _logs = MutableSharedFlow<String>(extraBufferCapacity = 64)
    val logs: SharedFlow<String> = _logs.asSharedFlow()

    private var socket: Socket? = null
    private var output: BufferedOutputStream? = null
    private var connectionJob: Job? = null

    // 串行化写操作（保证帧按序发出）；seq 单调递增
    private val writeMutex = Mutex()
    private val seqCounter = AtomicInteger(0)

    /** 连接服务端；连接中/已连接时重复调用会被忽略 */
    fun connect(host: String, port: Int) {
        val state = _connectionState.value
        if (state is ConnectionState.Connecting || state is ConnectionState.Connected) return

        connectionJob?.cancel()
        _connectionState.value = ConnectionState.Connecting
        emitLog("正在连接 $host:$port ...")

        connectionJob = scope.launch {
            val sock = try {
                val s = Socket()
                s.tcpNoDelay = true
                withContext(Dispatchers.IO) {
                    s.connect(InetSocketAddress(host, port), CONNECT_TIMEOUT_MS)
                }
                s
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                _connectionState.value = ConnectionState.Disconnected(e.message ?: "连接失败")
                emitLog("连接失败: ${e.message}")
                return@launch
            }

            socket = sock
            _connectionState.value = ConnectionState.Connected
            emitLog("已连接 $host:$port")
            runReadLoop(sock)
        }
    }

    /** 主动断开连接（幂等） */
    fun disconnect() {
        val wasActive = _connectionState.value is ConnectionState.Connecting ||
            _connectionState.value is ConnectionState.Connected
        connectionJob?.cancel()
        connectionJob = null
        runCatching { output?.close() }
        runCatching { socket?.close() }
        output = null
        socket = null
        if (wasActive) {
            _connectionState.value = ConnectionState.Disconnected("主动断开")
            emitLog("已断开连接")
        }
    }

    /** 发送一帧（自动分配 seq），返回该 seq；未连接时返回 -1 */
    fun send(msgType: Int, body: ByteArray): Int {
        val seq = seqCounter.incrementAndGet()
        send(msgType, seq, body)
        return seq
    }

    /** 发送一帧（指定 seq）。写操作抛到 IO 线程执行，避免主线程网络 IO 抛 NetworkOnMainThreadException */
    fun send(msgType: Int, seq: Int, body: ByteArray) {
        val current = socket ?: run {
            emitLog("发送失败: 尚未连接")
            return
        }
        val frame = encodePacket(msgType, seq, body)
        scope.launch {
            writeMutex.withLock {
                try {
                    // 复用同一输出流，避免每次发送都重新包装底层 OutputStream
                    val out = output ?: BufferedOutputStream(current.getOutputStream()).also { output = it }
                    out.write(frame)
                    out.flush()
                    emitLog("发送 0x${msgType.toString(16).padStart(4, '0')} seq=$seq (${body.size} B)")
                } catch (e: Exception) {
                    // 写失败 => 连接已损坏。别只打印 e.message（可能为 null），
                    // 打异常类型 + 完整堆栈，并同步触发断线清理（否则状态仍停留「已连接」）。
                    Log.e(TAG, "发送失败 class=${e.javaClass.name}", e)
                    emitLog("发送失败: ${e.javaClass.simpleName}: ${e.message ?: "(无 detail message)"}")
                    handleDisconnect(e.message ?: "写失败 ${e.javaClass.simpleName}")
                }
            }
        }
    }

    /** 阻塞读循环：喂 PacketDecoder 切帧，完整包上抛 */
    private suspend fun runReadLoop(sock: Socket) {
        val input = BufferedInputStream(sock.getInputStream())
        val buffer = ByteArray(READ_BUFFER_SIZE)
        val decoder = PacketDecoder()
        try {
            while (true) {
                val count = withContext(Dispatchers.IO) { input.read(buffer) }
                if (count < 0) {
                    handleDisconnect("服务端关闭连接")
                    return
                }
                val packets = decoder.feed(buffer, count)
                if (decoder.hasError) {
                    emitLog("协议错误，断开连接")
                    handleDisconnect("协议错误")
                    return
                }
                packets.forEach { _packets.tryEmit(it) }
            }
        } catch (e: CancellationException) {
            throw e
        } catch (e: SocketException) {
            handleDisconnect(e.message ?: "连接中断")
        } catch (e: Exception) {
            handleDisconnect(e.message ?: "读取异常")
        }
    }

    private fun handleDisconnect(reason: String) {
        // 幂等：只有「连接中/已连接」才真正转换状态并记日志，
        // 避免 send 与读循环同时感知断开时重复打印/重复置状态。
        val wasActive = _connectionState.value is ConnectionState.Connecting ||
            _connectionState.value is ConnectionState.Connected
        runCatching { output?.close() }
        runCatching { socket?.close() }
        output = null
        socket = null
        if (wasActive) {
            _connectionState.value = ConnectionState.Disconnected(reason)
            emitLog("断开连接: $reason")
        }
    }

    private fun emitLog(message: String) {
        _logs.tryEmit(message)
    }

    companion object {
        private const val TAG = "TcpClient"
        private const val CONNECT_TIMEOUT_MS = 10_000
        private const val READ_BUFFER_SIZE = 4096
    }
}
