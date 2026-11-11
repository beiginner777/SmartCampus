package com.example.smartcampus.data

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.intPreferencesKey
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.map
import kotlinx.serialization.Serializable

// 顶层扩展：每个进程只创建一个 DataStore 实例
private val Context.dataStore: DataStore<Preferences> by preferencesDataStore(name = "settings")

/**
 * 服务端地址配置仓库（Preferences DataStore）。
 *
 * 持久化服务端 host + 网关 TCP 端口 + Auth HTTP 端口，供登录/重连使用。
 */
class SettingsRepository(private val context: Context) {

    /** 服务端地址配置（首次启动返回默认值） */
    val serverConfig: Flow<ServerConfig> = context.dataStore.data.map { prefs ->
        ServerConfig(
            host = prefs[Keys.SERVER_HOST] ?: DEFAULT_HOST,
            gatewayPort = prefs[Keys.SERVER_GATEWAY_PORT] ?: DEFAULT_GATEWAY_PORT,
            authPort = prefs[Keys.SERVER_AUTH_PORT] ?: DEFAULT_AUTH_PORT,
        )
    }

    suspend fun saveServerConfig(config: ServerConfig) {
        context.dataStore.edit { prefs ->
            prefs[Keys.SERVER_HOST] = config.host
            prefs[Keys.SERVER_GATEWAY_PORT] = config.gatewayPort
            prefs[Keys.SERVER_AUTH_PORT] = config.authPort
        }
    }

    private object Keys {
        val SERVER_HOST = stringPreferencesKey("server_host")
        val SERVER_GATEWAY_PORT = intPreferencesKey("server_gateway_port")
        val SERVER_AUTH_PORT = intPreferencesKey("server_auth_port")
    }

    /** 服务端连接配置 */
    @Serializable
    data class ServerConfig(
        val host: String,
        val gatewayPort: Int,
        val authPort: Int,
    ) {
        /** HTTP AuthServer base URL */
        val authBaseUrl: String get() = "http://$host:$authPort"
    }

    companion object {
        // 模拟器访问宿主机回环用 10.0.2.2；真机联调改为服务端所在电脑的局域网 IP
        const val DEFAULT_HOST = "10.40.95.46"
        const val DEFAULT_GATEWAY_PORT = 9000
        const val DEFAULT_AUTH_PORT = 8080
    }
}
