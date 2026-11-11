package com.example.smartcampus.ui.auth

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
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
fun RegisterScreen(viewModel: AuthViewModel, onBackToLogin: () -> Unit) {
    val config by viewModel.serverConfig.collectAsState()
    val isLoading by viewModel.isLoading.collectAsState()
    val error by viewModel.error.collectAsState()
    val successMessage by viewModel.successMessage.collectAsState()

    var host by rememberSaveable { mutableStateOf(config?.host ?: "") }
    var gatewayPort by rememberSaveable { mutableStateOf(config?.gatewayPort?.toString() ?: "") }
    var authPort by rememberSaveable { mutableStateOf(config?.authPort?.toString() ?: "") }
    var username by rememberSaveable { mutableStateOf("") }
    var password by rememberSaveable { mutableStateOf("") }
    var nickname by rememberSaveable { mutableStateOf("") }
    var tags by rememberSaveable { mutableStateOf("") }
    var role by rememberSaveable { mutableIntStateOf(0) }  // 0=学生 1=教师

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
            Text("注册", style = MaterialTheme.typography.headlineMedium)

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
            OutlinedTextField(
                value = nickname,
                onValueChange = { nickname = it },
                modifier = Modifier.fillMaxWidth(),
                label = { Text("昵称") },
                singleLine = true,
                enabled = !isLoading,
            )
            OutlinedTextField(
                value = tags,
                onValueChange = { tags = it },
                modifier = Modifier.fillMaxWidth(),
                label = { Text("标签（如 跑步/篮球，逗号分隔）") },
                singleLine = true,
                enabled = !isLoading,
            )

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                FilterChip(selected = role == 0, onClick = { role = 0 }, label = { Text("学生") })
                FilterChip(selected = role == 1, onClick = { role = 1 }, label = { Text("教师") })
            }

            if (error != null) {
                Text(error!!, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodyMedium)
            }
            if (successMessage != null) {
                Text(successMessage!!, color = MaterialTheme.colorScheme.primary, style = MaterialTheme.typography.bodyMedium)
            }

            Button(
                onClick = {
                    viewModel.register(
                        SettingsRepository.ServerConfig(host.trim(), gatewayPortInt!!, authPortInt!!),
                        username.trim(),
                        password,
                        nickname.trim(),
                        role,
                        tags.trim(),
                    )
                },
                modifier = Modifier.fillMaxWidth(),
                enabled = canSubmit,
            ) {
                Text(if (isLoading) "注册中..." else "注册")
            }

            TextButton(
                onClick = onBackToLogin,
                modifier = Modifier.align(Alignment.CenterHorizontally)
            ) {
                Text("已有账号？去登录")
            }
        }
    }
}
