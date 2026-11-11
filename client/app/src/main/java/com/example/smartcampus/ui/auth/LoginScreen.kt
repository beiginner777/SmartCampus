package com.example.smartcampus.ui.auth

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import com.example.smartcampus.data.SettingsRepository

@Composable
fun LoginScreen(viewModel: AuthViewModel, onGoRegister: () -> Unit) {
    val config by viewModel.serverConfig.collectAsState()
    val isLoading by viewModel.isLoading.collectAsState()
    val error by viewModel.error.collectAsState()

    var host by rememberSaveable { mutableStateOf(config?.host ?: "") }
    var gatewayPort by rememberSaveable { mutableStateOf(config?.gatewayPort?.toString() ?: "") }
    var authPort by rememberSaveable { mutableStateOf(config?.authPort?.toString() ?: "") }
    var username by rememberSaveable { mutableStateOf("") }
    var password by rememberSaveable { mutableStateOf("") }

    LaunchedEffect(config) {
        val cfg = config
        if (host.isBlank() && cfg != null) host = cfg.host
        if (gatewayPort.isBlank() && cfg != null) gatewayPort = cfg.gatewayPort.toString()
        if (authPort.isBlank() && cfg != null) authPort = cfg.authPort.toString()
    }

    val gatewayPortInt = gatewayPort.toIntOrNull()
    val authPortInt = authPort.toIntOrNull()
    val canSubmit = host.isNotBlank() && gatewayPortInt != null && authPortInt != null &&
        username.isNotBlank() && password.isNotBlank() && !isLoading

    Scaffold(modifier = Modifier.fillMaxSize()) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(24.dp)
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Text("登录", style = MaterialTheme.typography.headlineMedium)

            ServerConfigFields(
                host = host, onHostChange = { host = it },
                gatewayPort = gatewayPort, onGatewayPortChange = { gatewayPort = it },
                authPort = authPort, onAuthPortChange = { authPort = it },
                enabled = !isLoading,
            )

            OutlinedTextField(
                value = username,
                onValueChange = { username = it },
                modifier = Modifier.fillMaxWidth(),
                label = { Text("用户名") },
                singleLine = true,
                enabled = !isLoading,
            )
            OutlinedTextField(
                value = password,
                onValueChange = { password = it },
                modifier = Modifier.fillMaxWidth(),
                label = { Text("密码") },
                singleLine = true,
                visualTransformation = PasswordVisualTransformation(),
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password),
                enabled = !isLoading,
            )

            if (error != null) {
                Text(
                    text = error!!,
                    color = MaterialTheme.colorScheme.error,
                    style = MaterialTheme.typography.bodyMedium
                )
            }

            Button(
                onClick = {
                    viewModel.login(
                        SettingsRepository.ServerConfig(host.trim(), gatewayPortInt!!, authPortInt!!),
                        username.trim(),
                        password,
                    )
                },
                modifier = Modifier.fillMaxWidth(),
                enabled = canSubmit,
            ) {
                Text(if (isLoading) "登录中..." else "登录")
            }

            TextButton(
                onClick = onGoRegister,
                modifier = Modifier.align(Alignment.CenterHorizontally)
            ) {
                Text("没有账号？去注册")
            }
        }
    }
}

/** 服务端配置输入（登录/注册共用） */
@Composable
fun ServerConfigFields(
    host: String,
    onHostChange: (String) -> Unit,
    gatewayPort: String,
    onGatewayPortChange: (String) -> Unit,
    authPort: String,
    onAuthPortChange: (String) -> Unit,
    enabled: Boolean,
) {
    Text("服务器", style = MaterialTheme.typography.titleSmall)
    OutlinedTextField(
        value = host,
        onValueChange = onHostChange,
        modifier = Modifier.fillMaxWidth(),
        label = { Text("服务器 IP") },
        singleLine = true,
        enabled = enabled,
    )
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        OutlinedTextField(
            value = gatewayPort,
            onValueChange = onGatewayPortChange,
            modifier = Modifier.width(150.dp),
            label = { Text("网关端口") },
            singleLine = true,
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
            enabled = enabled,
        )
        OutlinedTextField(
            value = authPort,
            onValueChange = onAuthPortChange,
            modifier = Modifier.width(150.dp),
            label = { Text("鉴权端口") },
            singleLine = true,
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
            enabled = enabled,
        )
    }
}
