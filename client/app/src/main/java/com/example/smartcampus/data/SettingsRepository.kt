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
 * 本地配置仓库（Preferences DataStore）。
 *
 * M0 用于持久化服务端地址/端口，供 echo 联调界面预填；
 * M1 起会扩展：JWT 令牌存储、登录态等。
 */
class SettingsRepository(private val context: Context) {

    /** 最近一次保存的服务端配置（首次启动返回默认值） */
    val serverConfig: Flow<ServerConfig> = context.dataStore.data.map { prefs ->
        ServerConfig(
            host = prefs[Keys.SERVER_HOST] ?: DEFAULT_HOST,
            port = prefs[Keys.SERVER_PORT] ?: DEFAULT_PORT,
        )
    }

    suspend fun saveServerConfig(host: String, port: Int) {
        context.dataStore.edit { prefs ->
            prefs[Keys.SERVER_HOST] = host
            prefs[Keys.SERVER_PORT] = port
        }
    }

    private object Keys {
        val SERVER_HOST = stringPreferencesKey("server_host")
        val SERVER_PORT = intPreferencesKey("server_port")
    }

    /**
     * 服务端连接配置。
     *
     * 标注 [Serializable]：M1 的 AuthServer 走 HTTP + JSON，届时此配置可直接序列化交互；
     * M0 仅引入序列化依赖并在此落地，验证插件链路。
     */
    @Serializable
    data class ServerConfig(val host: String, val port: Int)

    companion object {
        // 模拟器访问宿主机回环用 10.0.2.2；真机联调改为服务端所在电脑的局域网 IP
        const val DEFAULT_HOST = "10.0.2.2"
        const val DEFAULT_PORT = 9000
    }
}
