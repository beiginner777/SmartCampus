package com.example.smartcampus.ui.home

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.example.smartcampus.data.Session
import com.example.smartcampus.network.ConnectionState
import com.example.smartcampus.network.GatewayState

/**
 * 主界面：登录后展示用户信息 + 两步连接进度（连接网关 / 鉴权），并处理「已被踢」弹窗。
 */
@Composable
fun HomeScreen(session: Session, viewModel: HomeViewModel) {
    val gatewayState by viewModel.gatewayState.collectAsState()
    val connectionState by viewModel.connectionState.collectAsState()
    val kickMessage by viewModel.kickMessage.collectAsState()

    LaunchedEffect(session.token) {
        viewModel.connect(session)
    }

    // 步骤① 连接网关（传输层）
    val gatewayStep = when (val s = connectionState) {
        ConnectionState.Idle -> StepStatus("未连接", Color(0xFF9E9E9E))
        ConnectionState.Connecting -> StepStatus("连接中...", Color(0xFFEF6C00))
        ConnectionState.Connected -> StepStatus("已连接", Color(0xFF2E7D32), done = true)
        is ConnectionState.Disconnected -> StepStatus("失败/断开：${s.reason}", Color(0xFFC62828))
    }
    // 步骤② 鉴权（会话层）
    val authStep = when (gatewayState) {
        GatewayState.Authing -> StepStatus("鉴权中...", Color(0xFFEF6C00))
        GatewayState.Authed -> StepStatus("已鉴权", Color(0xFF2E7D32), done = true)
        else -> StepStatus("未鉴权", Color(0xFF9E9E9E))
    }
    val roleText = if (session.role == 1) "教师" else "学生"

    Scaffold(modifier = Modifier.fillMaxSize()) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(24.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            Text("智慧校园", style = MaterialTheme.typography.headlineMedium)

            // 用户信息
            Surface(
                modifier = Modifier.fillMaxWidth(),
                shape = MaterialTheme.shapes.medium,
                color = MaterialTheme.colorScheme.surfaceVariant
            ) {
                Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    Text(
                        text = session.nickname.ifBlank { session.username },
                        style = MaterialTheme.typography.titleMedium
                    )
                    Text(
                        text = "账号：${session.username} ｜ 身份：$roleText",
                        style = MaterialTheme.typography.bodySmall
                    )
                }
            }

            // 两步连接进度
            Surface(
                modifier = Modifier.fillMaxWidth(),
                shape = MaterialTheme.shapes.medium,
                color = MaterialTheme.colorScheme.surface
            ) {
                Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    StepRow("① 连接网关", gatewayStep)
                    StepRow("② 鉴权", authStep)
                }
            }

            Button(onClick = viewModel::logout, modifier = Modifier.fillMaxWidth()) {
                Text("退出登录")
            }
        }
    }

    if (kickMessage != null) {
        AlertDialog(
            onDismissRequest = viewModel::dismissKick,
            title = { Text("连接已断开") },
            text = { Text(kickMessage!!) },
            confirmButton = {
                TextButton(onClick = viewModel::dismissKick) { Text("确定") }
            }
        )
    }
}

/** 某一步的状态（文案 + 颜色 + 是否完成） */
private data class StepStatus(val text: String, val color: Color, val done: Boolean = false)

@Composable
private fun StepRow(label: String, status: StepStatus) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(
            text = "$label：",
            style = MaterialTheme.typography.bodyLarge,
            color = MaterialTheme.colorScheme.onSurface
        )
        Text(
            text = status.text,
            style = MaterialTheme.typography.bodyLarge,
            color = status.color,
            fontWeight = if (status.done) FontWeight.Bold else FontWeight.Normal
        )
    }
}
