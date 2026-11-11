package com.example.smartcampus.ui.home

import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import com.example.smartcampus.data.AuthRepository
import com.example.smartcampus.data.Session
import com.example.smartcampus.data.SettingsRepository
import com.example.smartcampus.di.AppContainer
import com.example.smartcampus.network.ConnectionState
import com.example.smartcampus.network.GatewayClient
import com.example.smartcampus.network.GatewayEvent
import com.example.smartcampus.network.GatewayState
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch

/**
 * 主界面 ViewModel：管理 TCP 会话（连接鉴权 / 心跳 / 互踢）与登出。
 */
class HomeViewModel(
    private val gatewayClient: GatewayClient,
    private val authRepository: AuthRepository,
    private val settingsRepository: SettingsRepository,
) : ViewModel() {

    /** 会话层状态（连接中 / 鉴权中 / 已连接 / 已断开） */
    val gatewayState: StateFlow<GatewayState> = gatewayClient.state

    /** 传输层连接状态（连接网关这一步的进度/结果） */
    val connectionState: StateFlow<ConnectionState> = gatewayClient.connectionState

    /** 被踢 / 鉴权失败提示（非空时弹窗） */
    private val _kickMessage = MutableStateFlow<String?>(null)
    val kickMessage: StateFlow<String?> = _kickMessage.asStateFlow()

    private var connectedToken: String? = null

    init {
        viewModelScope.launch {
            gatewayClient.events.collect { ev ->
                when (ev) {
                    is GatewayEvent.Kicked -> _kickMessage.value = ev.reason
                    is GatewayEvent.AuthFailed -> _kickMessage.value = "鉴权失败：${ev.msg}"
                }
            }
        }
    }

    /** 用当前会话连网关（幂等：同一 token 不重复连） */
    fun connect(session: Session) {
        if (connectedToken == session.token) return
        connectedToken = session.token
        viewModelScope.launch {
            val config = settingsRepository.serverConfig.first()
            gatewayClient.connect(config.host, config.gatewayPort, session.token)
        }
    }

    fun logout() {
        connectedToken = null
        viewModelScope.launch {
            gatewayClient.logout()
            authRepository.clear()
        }
    }

    fun dismissKick() {
        _kickMessage.value = null
    }

    companion object {
        fun factory(container: AppContainer): ViewModelProvider.Factory =
            object : ViewModelProvider.Factory {
                @Suppress("UNCHECKED_CAST")
                override fun <T : ViewModel> create(modelClass: Class<T>): T =
                    HomeViewModel(container.gatewayClient, container.authRepository, container.settingsRepository) as T
            }
    }
}
