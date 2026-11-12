package com.example.smartcampus.di

import android.content.Context
import com.example.smartcampus.data.AuthApi
import com.example.smartcampus.data.AuthRepository
import com.example.smartcampus.data.SettingsRepository
import com.example.smartcampus.location.LocationManager
import com.example.smartcampus.network.GatewayClient
import com.example.smartcampus.network.TcpClient
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.serialization.json.Json

/**
 * 手写依赖注入容器。
 *
 * 集中创建并持有各层单例依赖：
 *   - [settingsRepository]：服务端地址配置（DataStore）
 *   - [authRepository]：登录会话（token/用户信息，DataStore）
 *   - [authApi]：HTTP 鉴权客户端（AuthServer）
 *   - [tcpClient]：TCP 传输层（二进制分帧 + 粘包/拆包）
 *   - [gatewayClient]：会话层（鉴权握手 + 心跳 + 重连 + 互踢 + 坐标上报 + 附近用户）
 *   - [locationManager]：高德持续定位（M2）
 */
class AppContainer(context: Context) {

    private val applicationScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val json = Json { ignoreUnknownKeys = true }

    val settingsRepository: SettingsRepository = SettingsRepository(context)
    val authRepository: AuthRepository = AuthRepository(context)
    val authApi: AuthApi = AuthApi(json)
    val tcpClient: TcpClient = TcpClient(applicationScope)
    val gatewayClient: GatewayClient = GatewayClient(tcpClient, json, applicationScope)
    val locationManager: LocationManager = LocationManager(context)
}
