package com.example.smartcampus.location

import android.content.Context
import com.amap.api.location.AMapLocation
import com.amap.api.location.AMapLocationClient
import com.amap.api.location.AMapLocationClientOption
import com.amap.api.location.AMapLocationListener
import com.amap.api.maps.model.LatLng
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.asSharedFlow

/**
 * 定位管理（M2 接入）：包装高德 [AMapLocationClient]，持续定位并暴露坐标流。
 *
 * 职责：权限申请（由 Activity 完成）→ 持续定位 → 通过 [locations] 抛出坐标，
 * 上层（MapViewModel）据此周期上报到服务端 AOI。
 */
class LocationManager(private val context: Context) {

    private val _locations = MutableSharedFlow<LatLng>(extraBufferCapacity = 16)
    val locations: SharedFlow<LatLng> = _locations.asSharedFlow()

    private var client: AMapLocationClient? = null

    private val listener = AMapLocationListener { location ->
        if (location == null || location.errorCode != AMapLocation.LOCATION_SUCCESS) return@AMapLocationListener
        _locations.tryEmit(LatLng(location.latitude, location.longitude))
    }

    fun start() {
        if (client != null) return
        val c = AMapLocationClient(context.applicationContext)
        c.setLocationOption(
            AMapLocationClientOption().apply {
                locationMode = AMapLocationClientOption.AMapLocationMode.Hight_Accuracy
                interval = 3_000  // 3s 一次，与服务端坐标上报节流一致
                isOnceLocation = false
            }
        )
        c.setLocationListener(listener)
        c.startLocation()
        client = c
    }

    fun stop() {
        client?.stopLocation()
        client?.onDestroy()
        client = null
    }
}
