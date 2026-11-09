package com.example.smartcampus

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.material3.MaterialTheme
import com.example.smartcampus.ui.debug.EchoDebugScreen

/**
 * 智慧校园客户端入口。
 *
 * M0：渲染 TCP Echo 联调界面，打通「客户端连服务端 EchoServer → 发消息回显 → 断线打印日志」。
 * M2/M3 起地图、考勤、聊天等业务界面在本模块逐步接入。
 */
class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            MaterialTheme {
                EchoDebugScreen()
            }
        }
    }
}
