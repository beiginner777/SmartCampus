package com.example.smartcampus.data

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.intPreferencesKey
import androidx.datastore.preferences.core.longPreferencesKey
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.map

// 独立的 DataStore 文件，存登录会话（与服务器地址配置分开）
private val Context.authDataStore: DataStore<Preferences> by preferencesDataStore(name = "auth")

/** 登录会话（登录成功后持久化，重连鉴权/自动登录时使用） */
data class Session(
    val token: String,
    val uid: Long,
    val username: String,
    val nickname: String,
    val role: Int,
    val tags: String,
)

/**
 * 会话持久化仓库（Preferences DataStore）。
 *
 * 保存 JWT token 与用户信息，供「自动登录 / TCP 重连鉴权」使用。
 */
class AuthRepository(private val context: Context) {

    /** 当前会话；未登录时为 null */
    val session: Flow<Session?> = context.authDataStore.data.map { prefs ->
        val token = prefs[Keys.TOKEN] ?: return@map null
        Session(
            token = token,
            uid = prefs[Keys.UID] ?: 0,
            username = prefs[Keys.USERNAME] ?: "",
            nickname = prefs[Keys.NICKNAME] ?: "",
            role = prefs[Keys.ROLE] ?: 0,
            tags = prefs[Keys.TAGS] ?: "",
        )
    }

    suspend fun save(session: Session) {
        context.authDataStore.edit { prefs ->
            prefs[Keys.TOKEN] = session.token
            prefs[Keys.UID] = session.uid
            prefs[Keys.USERNAME] = session.username
            prefs[Keys.NICKNAME] = session.nickname
            prefs[Keys.ROLE] = session.role
            prefs[Keys.TAGS] = session.tags
        }
    }

    suspend fun clear() {
        context.authDataStore.edit { it.clear() }
    }

    private object Keys {
        val TOKEN = stringPreferencesKey("token")
        val UID = longPreferencesKey("uid")
        val USERNAME = stringPreferencesKey("username")
        val NICKNAME = stringPreferencesKey("nickname")
        val ROLE = intPreferencesKey("role")
        val TAGS = stringPreferencesKey("tags")
    }
}
