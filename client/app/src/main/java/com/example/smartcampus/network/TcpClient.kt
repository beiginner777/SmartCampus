package com.example.smartcampus.network

import android.util.Log
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

/**
 * 原生 TCP 客户端（M0：对接服务端 EchoServer）。
 *
 * 采用 `java.net.Socket` + 协程，而非 OkHttp（OkHttp 不支持裸 TCP 长连接）。
 * 与服务端 C++ `boost/asio` 网关直连：M0 服务端仅做原样回显（echo），
 * M1 起在此之上叠加自定义二进制协议（长度前缀 + 消息类型 + 序列号 + body，见 [codec] TODO）。
 *
 * 职责：连接管理、异步读循环、发送、断线回调，状态/日志通过 Flow 暴露给上层。
 */
class TcpClient(private val scope: CoroutineScope) {

    /** 连接状态（UI 直接订阅） */
    private val _connectionState = MutableStateFlow<ConnectionState>(ConnectionState.Idle)
    val connectionState: StateFlow<ConnectionState> = _connectionState.asStateFlow()

    /** 日志流：连接 / 发送 / 回显 / 断线等事件，供 UI 展示 */
    private val _logs = MutableSharedFlow<String>(extraBufferCapacity = 64)
    val logs: SharedFlow<String> = _logs.asSharedFlow()

    private var socket: Socket? = null
    private var output: BufferedOutputStream? = null
    private var connectionJob: Job? = null

    // 串行化写操作：M0 防止并发写同一输出流，M1 起保证二进制协议帧按序发出
    private val writeMutex = Mutex()

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

    /** 发送一条消息；未连接时忽略。写操作抛到 IO 线程执行，避免主线程网络 IO 抛 NetworkOnMainThreadException */
    fun send(text: String) {
        val current = socket ?: run {
            emitLog("发送失败: 尚未连接")
            return
        }
        scope.launch {
            writeMutex.withLock {
                try {
                    val bytes = text.toByteArray(Charsets.UTF_8)
                    // 复用同一输出流，避免每次发送都重新包装底层 OutputStream
                    val out = output ?: BufferedOutputStream(current.getOutputStream()).also { output = it }
                    out.write(bytes)
                    out.flush()
                    emitLog("发送(${bytes.size} B): $text")
                } catch (e: Exception) {
                    // 写失败 => 连接已损坏（对端关闭/网络中断）。别只打印 e.message（可能为 null），
                    // 打异常类型 + 完整堆栈，并同步触发断线清理（否则状态仍停留「已连接」）。
                    Log.e(TAG, "发送失败 class=${e.javaClass.name}", e)
                    emitLog("发送失败: ${e.javaClass.simpleName}: ${e.message ?: "(无 detail message)"}")
                    handleDisconnect(e.message ?: "写失败 ${e.javaClass.simpleName}")
                }
            }
        }
    }

    /** 阻塞读循环：读到数据原样打印回显，读到 EOF / 异常则判定断线 */
    private suspend fun runReadLoop(sock: Socket) {
        val input = BufferedInputStream(sock.getInputStream())
        val buffer = ByteArray(READ_BUFFER_SIZE)
        try {
            while (true) {
                val count = withContext(Dispatchers.IO) { input.read(buffer) }
                if (count < 0) {
                    handleDisconnect("服务端关闭连接")
                    return
                }
                val echoed = String(buffer, 0, count, Charsets.UTF_8)
                Log.d(TAG, "echo($count B): $echoed")
                emitLog("回显($count B): $echoed")
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
        private const val CONNECT_TIMEOUT_MS = 5_000
        private const val READ_BUFFER_SIZE = 4096
    }
}
