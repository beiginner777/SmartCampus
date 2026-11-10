package com.example.smartcampus.ui.debug

import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import com.example.smartcampus.data.SettingsRepository
import com.example.smartcampus.di.AppContainer
import com.example.smartcampus.network.ConnectionState
import com.example.smartcampus.network.TcpClient
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

/**
 * M0 echo 联调界面的 ViewModel。
 *
 * 职责：把 [TcpClient] 的连接状态/日志流桥接到 UI，并在连接时持久化服务端配置。
 */
class EchoDebugViewModel(
    private val tcpClient: TcpClient,
    private val settingsRepository: SettingsRepository,
) : ViewModel() {

    /** ----------- Model ------------- **/

    /** 连接状态（透传网络层） */
    val connectionState: StateFlow<ConnectionState> = tcpClient.connectionState

    /** 最近一次保存的服务端配置（用于预填输入框；尚未保存过则为 null） */
    val serverConfig: StateFlow<SettingsRepository.ServerConfig?> =
        settingsRepository.serverConfig
            .map<SettingsRepository.ServerConfig, SettingsRepository.ServerConfig?> { it }
            .stateIn(viewModelScope, SharingStarted.Eagerly, null)

    /** 累积的通信日志（最多保留 MAX_LOG_LINES 条） */
    private val _logLines = MutableStateFlow<List<String>>(emptyList())
    val logLines: StateFlow<List<String>> = _logLines.asStateFlow()

    init {
        viewModelScope.launch {
            tcpClient.logs.collect { line ->
                _logLines.update { (it + line).takeLast(MAX_LOG_LINES) }
            }
        }
    }

    /** ----------- View-Model ------------- **/

    fun connect(host: String, port: Int) {
        if (host.isBlank()) return
        viewModelScope.launch { settingsRepository.saveServerConfig(host, port) }
        tcpClient.connect(host, port)
    }

    fun disconnect() = tcpClient.disconnect()

    fun send(message: String) {
        if (message.isBlank()) return
        tcpClient.send(message)
    }

    companion object {
        private const val MAX_LOG_LINES = 200

        /** 手写 DI：从 [AppContainer] 组装 ViewModel（替代 Hilt 的 @HiltViewModel） */
        fun factory(container: AppContainer): ViewModelProvider.Factory =
            object : ViewModelProvider.Factory {
                @Suppress("UNCHECKED_CAST")
                override fun <T : ViewModel> create(modelClass: Class<T>): T =
                    EchoDebugViewModel(container.tcpClient, container.settingsRepository) as T
            }
    }
}
