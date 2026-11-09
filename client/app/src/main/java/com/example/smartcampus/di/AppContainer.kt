package com.example.smartcampus.di

import android.content.Context
import com.example.smartcampus.data.SettingsRepository
import com.example.smartcampus.network.TcpClient
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob

/**
 * 手写依赖注入容器。
 *
 * 集中创建并持有各层单例依赖，上层（Activity / ViewModel）从这里取用：
 *   - [settingsRepository]：DataStore 持久化（服务端地址等）
 *   - [tcpClient]：原生 Socket 长连接客户端（M0 echo，M1 起叠加二进制协议）
 *
 * 网络 IO 等长生命周期任务统一挂到 [applicationScope]（随进程存活，IO 线程）。
 */
class AppContainer(context: Context) {

    private val applicationScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    val settingsRepository: SettingsRepository = SettingsRepository(context)

    val tcpClient: TcpClient = TcpClient(applicationScope)
}
