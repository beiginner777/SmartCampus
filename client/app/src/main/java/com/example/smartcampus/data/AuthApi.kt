package com.example.smartcampus.data

import com.example.smartcampus.data.dto.ApiResponse
import com.example.smartcampus.data.dto.LoginRequest
import com.example.smartcampus.data.dto.LoginResponse
import com.example.smartcampus.data.dto.RefreshRequest
import com.example.smartcampus.data.dto.RefreshResponse
import com.example.smartcampus.data.dto.RegisterRequest
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.decodeFromString
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import java.net.HttpURLConnection
import java.net.URL

/**
 * 鉴权 HTTP 客户端（对接服务端 AuthServer，默认 `:8080`）。
 *
 * 用 `java.net.HttpURLConnection`（决策点 #7：弃用 OkHttp，保持「原生」风格）。
 * 网络 IO 一律在 [Dispatchers.IO] 执行；网络异常（连不上/超时）直接抛出，由调用方处理。
 */
class AuthApi(
    private val json: Json = Json { ignoreUnknownKeys = true },
) {

    suspend fun register(baseUrl: String, req: RegisterRequest): ApiResponse =
        post("$baseUrl/register", req)

    suspend fun login(baseUrl: String, req: LoginRequest): LoginResponse =
        post("$baseUrl/login", req)

    suspend fun refresh(baseUrl: String, req: RefreshRequest): RefreshResponse =
        post("$baseUrl/refresh", req)

    private suspend inline fun <reified B : Any, reified T> post(url: String, body: B): T =
        withContext(Dispatchers.IO) {
            val conn = (URL(url).openConnection() as HttpURLConnection).apply {
                requestMethod = "POST"
                doOutput = true
                connectTimeout = 10_000
                readTimeout = 10_000
                setRequestProperty("Content-Type", "application/json")
            }
            try {
                conn.outputStream.use { it.write(json.encodeToString(body).toByteArray(Charsets.UTF_8)) }
                val status = conn.responseCode
                val stream = if (status in 200..299) conn.inputStream else conn.errorStream
                val text = stream?.bufferedReader()?.use { it.readText() } ?: ""
                json.decodeFromString<T>(text)
            }
            finally {
                conn.disconnect()
            }
        }
}
