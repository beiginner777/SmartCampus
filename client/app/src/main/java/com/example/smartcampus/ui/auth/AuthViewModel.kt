package com.example.smartcampus.ui.auth

import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import com.example.smartcampus.data.AuthApi
import com.example.smartcampus.data.AuthRepository
import com.example.smartcampus.data.Session
import com.example.smartcampus.data.SettingsRepository
import com.example.smartcampus.data.dto.LoginRequest
import com.example.smartcampus.data.dto.RegisterRequest
import com.example.smartcampus.di.AppContainer
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/**
 * 登录 / 注册 ViewModel。
 *
 * 通过 HTTP AuthServer 完成鉴权：登录成功后把会话写入 [AuthRepository]，
 * 由顶层 [com.example.smartcampus.ui.RootScreen] 监听会话流转到主界面。
 */
class AuthViewModel(
    private val authApi: AuthApi,
    private val authRepository: AuthRepository,
    private val settingsRepository: SettingsRepository,
) : ViewModel() {

    /** 服务端配置（预填输入框） */
    val serverConfig: StateFlow<SettingsRepository.ServerConfig?> =
        settingsRepository.serverConfig
            .map<SettingsRepository.ServerConfig, SettingsRepository.ServerConfig?> { it }
            .stateIn(viewModelScope, SharingStarted.Eagerly, null)

    private val _isLoading = MutableStateFlow(false)
    val isLoading: StateFlow<Boolean> = _isLoading.asStateFlow()

    private val _error = MutableStateFlow<String?>(null)
    val error: StateFlow<String?> = _error.asStateFlow()

    private val _successMessage = MutableStateFlow<String?>(null)
    val successMessage: StateFlow<String?> = _successMessage.asStateFlow()

    fun login(config: SettingsRepository.ServerConfig, username: String, password: String) {
        if (username.isBlank() || password.isBlank()) {
            _error.value = "用户名和密码不能为空"
            return
        }
        launchRequest {
            val resp = authApi.login(config.authBaseUrl, LoginRequest(username, password))
            if (resp.code == 0) {
                authRepository.save(
                    Session(
                        token = resp.token,
                        uid = resp.uid,
                        username = resp.username,
                        nickname = resp.nickname,
                        role = resp.role,
                        tags = resp.tags,
                    )
                )
                settingsRepository.saveServerConfig(config)
            } else {
                _error.value = resp.msg.ifBlank { "登录失败（code=${resp.code}）" }
            }
        }
    }

    fun register(
        config: SettingsRepository.ServerConfig,
        username: String,
        password: String,
        nickname: String,
        role: Int,
        tags: String,
    ) {
        if (username.isBlank() || password.isBlank()) {
            _error.value = "用户名和密码不能为空"
            return
        }
        launchRequest {
            val resp = authApi.register(
                config.authBaseUrl,
                RegisterRequest(username, password, role, nickname, tags),
            )
            if (resp.code == 0) {
                _successMessage.value = "注册成功，请登录"
                settingsRepository.saveServerConfig(config)
            } else {
                _error.value = resp.msg.ifBlank { "注册失败（code=${resp.code}）" }
            }
        }
    }

    fun clearError() {
        _error.value = null
    }

    fun clearSuccess() {
        _successMessage.value = null
    }

    private fun launchRequest(block: suspend () -> Unit) {
        _isLoading.value = true
        _error.value = null
        _successMessage.value = null
        viewModelScope.launch {
            try {
                block()
            } catch (e: Exception) {
                _error.value = "无法连接服务器：${e.message ?: e.javaClass.simpleName}"
            } finally {
                _isLoading.value = false
            }
        }
    }

    companion object {
        fun factory(container: AppContainer): ViewModelProvider.Factory =
            object : ViewModelProvider.Factory {
                @Suppress("UNCHECKED_CAST")
                override fun <T : ViewModel> create(modelClass: Class<T>): T =
                    AuthViewModel(container.authApi, container.authRepository, container.settingsRepository) as T
            }
    }
}
