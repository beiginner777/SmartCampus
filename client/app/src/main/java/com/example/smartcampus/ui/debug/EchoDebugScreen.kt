package com.example.smartcampus.ui.debug

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Button
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.example.smartcampus.SmartCampusApp
import com.example.smartcampus.network.ConnectionState

/**
 * M0 验收界面：连接服务端 EchoServer → 发消息回显 → 断线打印日志。
 *
 * 对应开发计划 M0 客户端验收链路，是 M1 二进制协议 / 鉴权的前置骨架。
 */
@Composable
fun EchoDebugScreen() {
    val app = LocalContext.current.applicationContext as SmartCampusApp
    val viewModel: EchoDebugViewModel = viewModel(factory = EchoDebugViewModel.factory(app.container))

    val connectionState by viewModel.connectionState.collectAsState()
    val logLines by viewModel.logLines.collectAsState()
    val serverConfig by viewModel.serverConfig.collectAsState()

    var host by rememberSaveable { mutableStateOf(serverConfig?.host ?: "") }
    var port by rememberSaveable { mutableStateOf(serverConfig?.port?.toString() ?: "") }
    var message by rememberSaveable { mutableStateOf("") }

    // 首次拿到已保存配置时预填（只在用户尚未输入时回填，不覆盖编辑中的内容）
    LaunchedEffect(serverConfig) {
        val cfg = serverConfig
        if (host.isBlank() && cfg != null) host = cfg.host
        if (port.isBlank() && cfg != null) port = cfg.port.toString()
    }

    val portInt = port.trim().toIntOrNull()
    val canConnect = host.isNotBlank() && portInt != null
    val isConnected = connectionState is ConnectionState.Connected
    val isConnecting = connectionState is ConnectionState.Connecting

    val (statusText, statusColor) = when (val s = connectionState) {
        is ConnectionState.Idle -> "未连接" to Color(0xFF9E9E9E)
        is ConnectionState.Connecting -> "连接中..." to Color(0xFFEF6C00)
        is ConnectionState.Connected -> "已连接" to Color(0xFF2E7D32)
        is ConnectionState.Disconnected -> "已断开：${s.reason}" to Color(0xFFC62828)
    }

    Scaffold(modifier = Modifier.fillMaxSize()) { innerPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Text("TCP Echo 联调", style = MaterialTheme.typography.titleLarge)

            // 连接状态提示条
            Surface(
                modifier = Modifier.fillMaxWidth(),
                shape = MaterialTheme.shapes.medium,
                color = statusColor.copy(alpha = 0.12f)
            ) {
                Text(
                    text = statusText,
                    modifier = Modifier.padding(horizontal = 12.dp, vertical = 8.dp),
                    style = MaterialTheme.typography.bodyMedium,
                    color = statusColor
                )
            }

            // 服务器地址 + 端口
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(
                    value = host,
                    onValueChange = { host = it },
                    modifier = Modifier.weight(1f),
                    label = { Text("服务器 IP") },
                    singleLine = true,
                    enabled = !isConnected && !isConnecting
                )
                OutlinedTextField(
                    value = port,
                    onValueChange = { port = it },
                    modifier = Modifier.width(110.dp),
                    label = { Text("端口") },
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                    enabled = !isConnected && !isConnecting
                )
            }

            // 连接 / 断开
            when {
                isConnecting -> Button(
                    onClick = {},
                    modifier = Modifier.fillMaxWidth(),
                    enabled = false
                ) { Text("连接中...") }

                isConnected -> Button(
                    onClick = viewModel::disconnect,
                    modifier = Modifier.fillMaxWidth()
                ) { Text("断开连接") }

                else -> Button(
                    onClick = { portInt?.let { viewModel.connect(host.trim(), it) } },
                    modifier = Modifier.fillMaxWidth(),
                    enabled = canConnect
                ) { Text("连接") }
            }

            // 消息输入 + 发送
            Row(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                OutlinedTextField(
                    value = message,
                    onValueChange = { message = it },
                    modifier = Modifier.weight(1f),
                    label = { Text("消息") },
                    singleLine = true,
                    enabled = isConnected
                )
                Button(
                    onClick = { viewModel.send(message.trim()); message = "" },
                    enabled = isConnected && message.isNotBlank()
                ) { Text("发送") }
            }

            HorizontalDivider()

            Text("通信日志", style = MaterialTheme.typography.titleMedium)
            LogList(logLines)
        }
    }
}

/**
 * 日志列表：滚动到底部展示最新一条，等宽字体便于阅读原始回显内容。
 */
@Composable
private fun ColumnScope.LogList(lines: List<String>) {
    if (lines.isEmpty()) {
        Text(
            text = "暂无日志，连接服务端后开始记录",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        return
    }
    val listState = rememberLazyListState()
    LaunchedEffect(lines.size) {
        listState.scrollToItem(lines.lastIndex)
    }
    LazyColumn(
        state = listState,
        modifier = Modifier
            .fillMaxWidth()
            .weight(1f)
    ) {
        items(lines) { line ->
            Text(
                text = line,
                style = MaterialTheme.typography.bodySmall,
                fontFamily = FontFamily.Monospace,
                modifier = Modifier.padding(vertical = 2.dp)
            )
        }
    }
}
