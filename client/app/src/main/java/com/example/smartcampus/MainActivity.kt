package com.example.smartcampus

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.material3.MaterialTheme
import com.example.smartcampus.ui.RootScreen

/**
 * 智慧校园客户端入口。
 *
 * M1：登录/注册 → TCP 鉴权 → 心跳 → 异地互踢全链路，入口为 [RootScreen]。
 */
class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            MaterialTheme {
                RootScreen()
            }
        }
    }
}
