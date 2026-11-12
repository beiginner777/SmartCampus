package com.example.smartcampus.ui.map

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import com.amap.api.maps.AMap
import com.amap.api.maps.CameraUpdateFactory
import com.amap.api.maps.TextureMapView
import com.amap.api.maps.model.BitmapDescriptorFactory
import com.amap.api.maps.model.CameraPosition
import com.amap.api.maps.model.LatLng
import com.amap.api.maps.model.Marker
import com.amap.api.maps.model.MarkerOptions
import com.example.smartcampus.data.Session
import com.example.smartcampus.data.dto.NearbyUser
import com.example.smartcampus.network.GatewayState
import kotlin.math.floor

/**
 * 地图主界面（M2）：登录后展示地图 + 附近用户头像（缩放聚合）+ 搜索高亮，持续定位并周期上报坐标。
 */
@Composable
fun MapScreen(session: Session, viewModel: MapViewModel) {
    val gatewayState by viewModel.gatewayState.collectAsState()
    val presence by viewModel.presence.collectAsState()
    val myLocation by viewModel.myLocation.collectAsState()
    val kickMessage by viewModel.kickMessage.collectAsState()

    val context = LocalContext.current
    val lifecycleOwner = LocalLifecycleOwner.current

    LaunchedEffect(session.token) { viewModel.connect(session) }

    DisposableEffect(Unit) {
        viewModel.startLocation()
        onDispose { viewModel.stopLocation() }
    }

    val mapView = remember { TextureMapView(context) }
    var aMap by remember { mutableStateOf<AMap?>(null) }
    var zoom by remember { mutableFloatStateOf(16f) }
    val zoomLevel = zoom.toInt()  // 只在整数缩放级变化时重建聚合
    var searchText by remember { mutableStateOf("") }

    // 同步高德 MapView 与 Compose 宿主生命周期
    DisposableEffect(lifecycleOwner) {
        val observer = LifecycleEventObserver { _, event ->
            when (event) {
                Lifecycle.Event.ON_CREATE -> mapView.onCreate(null)
                Lifecycle.Event.ON_RESUME -> mapView.onResume()
                Lifecycle.Event.ON_PAUSE -> mapView.onPause()
                Lifecycle.Event.ON_DESTROY -> mapView.onDestroy()
                else -> {}
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose {
            lifecycleOwner.lifecycle.removeObserver(observer)
            mapView.onDestroy()
        }
    }

    // 拿到 AMap 后配置一次 + 监听缩放
    LaunchedEffect(mapView) {
        val map = mapView.map ?: return@LaunchedEffect
        aMap = map
        map.uiSettings.apply {
            isZoomControlsEnabled = false
            isZoomGesturesEnabled = true
            isScrollGesturesEnabled = true
        }
        map.moveCamera(CameraUpdateFactory.newLatLngZoom(DEFAULT_CENTER, 16f))
        map.setOnCameraChangeListener(object : AMap.OnCameraChangeListener {
            override fun onCameraChange(position: CameraPosition) {
                zoom = position.zoom
            }
            override fun onCameraChangeFinish(position: CameraPosition) {
                zoom = position.zoom
            }
        })
    }

    // 首次定位后把视角移到「我的位置」
    LaunchedEffect(myLocation) {
        val loc = myLocation ?: return@LaunchedEffect
        val map = aMap ?: return@LaunchedEffect
        map.animateCamera(CameraUpdateFactory.newLatLngZoom(loc, 16f))
    }

    // 搜索：按昵称模糊匹配附近用户
    val searchResult = remember(searchText, presence) {
        if (searchText.isBlank()) null
        else presence.values.firstOrNull { it.nickname.contains(searchText.trim()) }
    }
    LaunchedEffect(searchResult) {
        val target = searchResult ?: return@LaunchedEffect
        val map = aMap ?: return@LaunchedEffect
        map.animateCamera(CameraUpdateFactory.newLatLngZoom(LatLng(target.lat, target.lng), 18f))
    }

    // 聚合 + 渲染附近用户 Marker（缩放级 / 附近用户 / 搜索高亮 变化时重建）
    val markers = remember { mutableListOf<Marker>() }
    LaunchedEffect(aMap, presence, zoomLevel, searchResult?.uid) {
        val map = aMap ?: return@LaunchedEffect
        markers.forEach { it.remove() }
        markers.clear()
        val highlightUid = searchResult?.uid
        clusterItems(presence.values.toList(), zoom.toFloat()).forEach { item ->
            val icon = when {
                item.count > 1 -> BitmapDescriptorFactory.fromBitmap(clusterBitmap(item.count, 56))
                item.uid == highlightUid -> BitmapDescriptorFactory.fromBitmap(avatarBitmap(item.nickname, 64))
                else -> BitmapDescriptorFactory.fromBitmap(avatarBitmap(item.nickname, 48))
            }
            markers.add(
                map.addMarker(
                    MarkerOptions()
                        .position(LatLng(item.lat, item.lng))
                        .anchor(0.5f, 0.5f)
                        .title(if (item.count > 1) "${item.count} 人" else item.nickname)
                        .icon(icon)
                )
            )
        }
    }

    val (statusText, statusColor) = when (gatewayState) {
        GatewayState.Idle -> "未连接" to Color(0xFF9E9E9E)
        GatewayState.Connecting -> "连接中..." to Color(0xFFEF6C00)
        GatewayState.Authing -> "鉴权中..." to Color(0xFFEF6C00)
        GatewayState.Authed -> "已连接" to Color(0xFF2E7D32)
        GatewayState.Disconnected -> "已断开" to Color(0xFFC62828)
    }
    val roleText = if (session.role == 1) "教师" else "学生"

    Box(Modifier.fillMaxSize()) {
        AndroidView(factory = { mapView }, modifier = Modifier.fillMaxSize())

        Surface(
            modifier = Modifier
                .align(Alignment.TopCenter)
                .fillMaxWidth(),
            color = MaterialTheme.colorScheme.surface.copy(alpha = 0.92f)
        ) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) {
                        Text(
                            text = "${session.nickname.ifBlank { session.username }}（$roleText）",
                            style = MaterialTheme.typography.titleMedium
                        )
                        Text(
                            text = "$statusText ｜ 附近 ${presence.size} 人 ｜ 缩放 $zoomLevel",
                            style = MaterialTheme.typography.bodySmall,
                            color = statusColor
                        )
                    }
                    TextButton(onClick = viewModel::logout) { Text("退出登录") }
                }
                OutlinedTextField(
                    value = searchText,
                    onValueChange = { searchText = it },
                    modifier = Modifier.fillMaxWidth(),
                    label = { Text("搜索附近用户（昵称）") },
                    singleLine = true,
                )
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

/** 一个待渲染的地图点：count>1 表示聚合簇 */
private data class ClusterItem(
    val count: Int,
    val lat: Double,
    val lng: Double,
    val nickname: String,
    val uid: Long,
)

/** 按缩放级别做网格聚合：缩放越高网格越细，聚合越少 */
private fun clusterItems(users: List<NearbyUser>, zoom: Float): List<ClusterItem> {
    if (users.isEmpty()) return emptyList()
    val cell = gridSizeForZoom(zoom)
    return users.groupBy { user ->
        val gx = floor(user.lng / cell).toInt()
        val gy = floor(user.lat / cell).toInt()
        gx to gy
    }.values.map { members ->
        if (members.size == 1) {
            val u = members[0]
            ClusterItem(1, u.lat, u.lng, u.nickname, u.uid)
        } else {
            ClusterItem(
                count = members.size,
                lat = members.map { it.lat }.average(),
                lng = members.map { it.lng }.average(),
                nickname = "",
                uid = -1,
            )
        }
    }
}

/** 缩放级别 → 网格边长（度）。缩放 17 约 50m，每降 1 级翻倍 */
private fun gridSizeForZoom(zoom: Float): Double {
    val level = zoom.toDouble()
    val exp = 17.0 - level
    return 0.0005 * Math.pow(2.0, exp)
}

/** 用昵称生成一个圆形头像（背景色取昵称哈希，中央显示首字符） */
private fun avatarBitmap(nickname: String, sizePx: Int): Bitmap {
    val bmp = Bitmap.createBitmap(sizePx, sizePx, Bitmap.Config.ARGB_8888)
    val canvas = Canvas(bmp)
    val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    paint.color = (nickname.hashCode() and 0xFFFFFF) or -0x1000000
    canvas.drawCircle(sizePx / 2f, sizePx / 2f, sizePx / 2f, paint)
    val initial = if (nickname.isNotEmpty()) nickname.substring(0, 1) else "?"
    paint.color = android.graphics.Color.WHITE
    paint.textSize = sizePx * 0.5f
    paint.textAlign = Paint.Align.CENTER
    val fm = paint.fontMetrics
    val baseline = sizePx / 2f - (fm.ascent + fm.descent) / 2f
    canvas.drawText(initial, sizePx / 2f, baseline, paint)
    return bmp
}

/** 聚合簇：半透明蓝色圆点 + 人数 */
private fun clusterBitmap(count: Int, sizePx: Int): Bitmap {
    val bmp = Bitmap.createBitmap(sizePx, sizePx, Bitmap.Config.ARGB_8888)
    val canvas = Canvas(bmp)
    val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    paint.color = 0xCC1976D2.toInt()
    canvas.drawCircle(sizePx / 2f, sizePx / 2f, sizePx / 2f, paint)
    paint.color = android.graphics.Color.WHITE
    paint.textSize = sizePx * 0.4f
    paint.textAlign = Paint.Align.CENTER
    val fm = paint.fontMetrics
    val baseline = sizePx / 2f - (fm.ascent + fm.descent) / 2f
    canvas.drawText("$count", sizePx / 2f, baseline, paint)
    return bmp
}

private val DEFAULT_CENTER = LatLng(39.9042, 116.4074)
