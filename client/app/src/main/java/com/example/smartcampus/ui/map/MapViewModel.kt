package com.example.smartcampus.ui.map

import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import com.amap.api.maps.model.LatLng
import com.example.smartcampus.data.AuthRepository
import com.example.smartcampus.data.Session
import com.example.smartcampus.data.SettingsRepository
import com.example.smartcampus.data.dto.NearbyUser
import com.example.smartcampus.di.AppContainer
import com.example.smartcampus.location.LocationManager
import com.example.smartcampus.network.ConnectionState
import com.example.smartcampus.network.GatewayClient
import com.example.smartcampus.network.GatewayEvent
import com.example.smartcampus.network.GatewayState
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch

/**
 * 地图界面 ViewModel：会话连接 + 持续定位 + 坐标上报 + 附近用户展示。
 */
class MapViewModel(
    private val gatewayClient: GatewayClient,
    private val authRepository: AuthRepository,
    private val settingsRepository: SettingsRepository,
    private val locationManager: LocationManager,
) : ViewModel() {

    /** 会话层状态 */
    val gatewayState: StateFlow<GatewayState> = gatewayClient.state

    /** 传输层连接状态 */
    val connectionState: StateFlow<ConnectionState> = gatewayClient.connectionState

    /** 附近在线用户（uid → 用户） */
    val presence: StateFlow<Map<Long, NearbyUser>> = gatewayClient.presence

    /** 我的当前位置（持续定位结果，用于地图居中 + 我的位置点） */
    private val _myLocation = MutableStateFlow<LatLng?>(null)
    val myLocation: StateFlow<LatLng?> = _myLocation.asStateFlow()

    private val _kickMessage = MutableStateFlow<String?>(null)
    val kickMessage: StateFlow<String?> = _kickMessage.asStateFlow()

    private var connectedToken: String? = null
    private var locationJob: Job? = null

    init {
        viewModelScope.launch {
            gatewayClient.events.collect { ev ->
                when (ev) {
                    is GatewayEvent.Kicked -> _kickMessage.value = ev.reason
                    is GatewayEvent.AuthFailed -> _kickMessage.value = "鉴权失败：${ev.msg}"
                }
            }
        }
    }

    /** 用当前会话连网关（幂等：同一 token 不重复连） */
    fun connect(session: Session) {
        if (connectedToken == session.token) return
        connectedToken = session.token
        viewModelScope.launch {
            val config = settingsRepository.serverConfig.first()
            gatewayClient.connect(config.host, config.gatewayPort, session.token)
        }
    }

    fun logout() {
        connectedToken = null
        stopLocation()
        viewModelScope.launch {
            gatewayClient.logout()
            authRepository.clear()
        }
    }

    /** 开始持续定位并把坐标上报到网关（sendPosition 内部只在已鉴权时才发） */
    fun startLocation() {
        if (locationJob != null) return
        locationJob = viewModelScope.launch {
            locationManager.locations.collect { latLng ->
                _myLocation.value = latLng
                gatewayClient.sendPosition(latLng.longitude, latLng.latitude)
            }
        }
        locationManager.start()
    }

    fun stopLocation() {
        locationJob?.cancel()
        locationJob = null
        locationManager.stop()
    }

    fun dismissKick() {
        _kickMessage.value = null
    }

    companion object {
        fun factory(container: AppContainer): ViewModelProvider.Factory =
            object : ViewModelProvider.Factory {
                @Suppress("UNCHECKED_CAST")
                override fun <T : ViewModel> create(modelClass: Class<T>): T =
                    MapViewModel(
                        container.gatewayClient,
                        container.authRepository,
                        container.settingsRepository,
                        container.locationManager,
                    ) as T
            }
    }
}
