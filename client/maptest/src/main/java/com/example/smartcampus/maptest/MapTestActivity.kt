package com.example.smartcampus.maptest

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.PorterDuff
import android.graphics.PorterDuffXfermode
import android.os.Build
import android.os.Bundle
import android.util.Log
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.FilledIconButton
import androidx.compose.material3.IconButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import com.amap.api.maps.AMap
import com.amap.api.maps.CameraUpdateFactory
import com.amap.api.maps.MapsInitializer
import com.amap.api.maps.TextureMapView
import com.amap.api.maps.model.BitmapDescriptorFactory
import com.amap.api.maps.model.CameraPosition
import com.amap.api.maps.model.LatLng
import com.amap.api.maps.model.MarkerOptions

private const val TAG = "MapTest"

/**
 * 地图最小验证入口。
 *
 * 目标：跑通「显示高德地图 + 双指/按钮缩放」这条最小链路，
 * 作为后续 AOI 聚合渲染、搜索高亮、地理围栏等地图功能的开发沙盒。
 */
class MapTestActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // 高德隐私合规：必须在调用任何地图接口前设置，否则地图不加载（errorCode 555570）
        MapsInitializer.updatePrivacyShow(this, true, true)
        MapsInitializer.updatePrivacyAgree(this, true)
        enableEdgeToEdge()
        setContent {
            MaterialTheme {
                MapTestScreen()
            }
        }
    }
}

@Composable
fun MapTestScreen() {
    val context = LocalContext.current
    val lifecycleOwner = LocalLifecycleOwner.current

    // 用 TextureView 实现，能正确与 Compose 覆盖层（缩放按钮/提示条）合成，避免 GLSurfaceView 的"挖洞"问题。
    val mapView = remember { TextureMapView(context) }
    var aMap by remember { mutableStateOf<AMap?>(null) }
    var zoom by remember { mutableFloatStateOf(16f) }

    // 高德只提供 ARM 的 so，x86 模拟器跑不了，先算出来给界面/日志提示用
    val isArmDevice = remember { Build.SUPPORTED_ABIS.any { it.contains("arm") } }
    // 地图是否加载完成（真机上可用于判断 key 鉴权是否成功）
    var mapLoaded by remember { mutableStateOf(false) }

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

    // 拿到 AMap 实例后做一次配置（onCreate 之后 getMap() 返回非空）
    LaunchedEffect(mapView) {
        if (!isArmDevice) {
            Log.w(TAG, "⚠️ 当前设备 ABI=[${Build.SUPPORTED_ABIS.joinToString()}] 不含 ARM，高德地图无法在 x86 模拟器上渲染")
        }
        val amap = mapView.map
        if (amap == null) {
            Log.e(TAG, "getMap() 返回 null，地图初始化失败")
            return@LaunchedEffect
        }
        Log.d(TAG, "getMap() 已返回 AMap 对象")
        aMap = amap
        amap.uiSettings.apply {
            isZoomControlsEnabled = false   // 用下方 Compose 按钮替代系统 +/- 控件
            isZoomGesturesEnabled = true    // 双指缩放
            isScrollGesturesEnabled = true  // 单指拖动
        }
        // ===== 1. 设置地图显示区域（校园）=====
        // TODO: 换成你们学校的经纬度
        val campus = LatLng(39.9042, 116.4074)
        amap.moveCamera(CameraUpdateFactory.newLatLngZoom(campus, 16f))

        // ===== 2. 在地图上显示头像 Marker，大小随缩放级别变化 =====
        // 加载原始头像（真实场景每个用户头像不同，这里统一用一个演示）
        val originalAvatar = BitmapFactory.decodeResource(context.resources, R.drawable.avatar)
        val markers = listOf(
            LatLng(39.9042, 116.4074) to "我",
            LatLng(39.9062, 116.4094) to "张三",
            LatLng(39.9022, 116.4054) to "李四",
        ).map { (pos, name) ->
            amap.addMarker(
                MarkerOptions()
                    .position(pos)
                    .anchor(0.5f, 0.5f)
                    .title(name)
            )
        }

        // 根据缩放级别重建头像图标（仅当尺寸变化时才重建，避免拖动时频繁刷新）
        var lastSize = 0
        fun updateAvatarSize(z: Float) {
            val size = avatarSizeForZoom(z)
            if (size == lastSize) return
            lastSize = size
            val icon = BitmapDescriptorFactory.fromBitmap(circleBitmap(originalAvatar, size))
            markers.forEach { it.setIcon(icon) }
        }
        updateAvatarSize(16f)

        amap.setOnCameraChangeListener(object : AMap.OnCameraChangeListener {
            override fun onCameraChange(position: CameraPosition) {
                zoom = position.zoom
                updateAvatarSize(position.zoom)
            }

            override fun onCameraChangeFinish(position: CameraPosition) {
                zoom = position.zoom
                updateAvatarSize(position.zoom)
            }
        })
        amap.setOnMapLoadedListener(object : AMap.OnMapLoadedListener {
            override fun onMapLoaded() {
                mapLoaded = true
                Log.w(TAG, "地图瓦片加载完成 ✅")
            }
        })
    }

    Box(modifier = Modifier.fillMaxSize()) {
        AndroidView(
            factory = { mapView },
            modifier = Modifier.fillMaxSize()
        )

        // 缩放控制（右下角）
        Column(
            modifier = Modifier
                .align(Alignment.CenterEnd)
                .padding(end = 16.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            ZoomButton(text = "+") { aMap?.moveCamera(CameraUpdateFactory.zoomIn()) }
            ZoomButton(text = "−") { aMap?.moveCamera(CameraUpdateFactory.zoomOut()) }
        }

        // 顶部提示条：实时显示当前缩放级别
        Surface(
            modifier = Modifier
                .align(Alignment.TopCenter)
                .padding(top = 16.dp),
            shape = MaterialTheme.shapes.medium,
            color = MaterialTheme.colorScheme.surface.copy(alpha = 0.85f)
        ) {
            Column(modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp)) {
                Text(
                    text = "缩放级别：${zoom.toInt()}",
                    style = MaterialTheme.typography.labelLarge
                )
                Text(
                    text = "ABI: ${Build.SUPPORTED_ABIS.joinToString()} ｜ 地图: ${if (mapLoaded) "已加载" else "未加载"}",
                    style = MaterialTheme.typography.labelSmall
                )
            }
        }

        // x86 模拟器上高德无法渲染，给出醒目提示
        if (!isArmDevice) {
            Surface(
                modifier = Modifier
                    .align(Alignment.BottomCenter)
                    .padding(16.dp),
                shape = MaterialTheme.shapes.medium,
                color = MaterialTheme.colorScheme.errorContainer
            ) {
                Text(
                    text = "⚠️ 当前是 x86 模拟器，高德地图无法渲染，请用 ARM 真机调试",
                    modifier = Modifier.padding(16.dp),
                    style = MaterialTheme.typography.labelLarge,
                    color = MaterialTheme.colorScheme.onErrorContainer
                )
            }
        }
    }
}

/**
 * 把图片缩放成指定尺寸并裁剪成圆形（用于头像 Marker）。
 */
private fun circleBitmap(src: Bitmap, size: Int): Bitmap {
    val scaled = Bitmap.createScaledBitmap(src, size, size, true)
    val out = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888)
    val canvas = Canvas(out)
    val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    canvas.drawCircle(size / 2f, size / 2f, size / 2f, paint)
    paint.xfermode = PorterDuffXfermode(PorterDuff.Mode.SRC_IN)
    canvas.drawBitmap(scaled, 0f, 0f, paint)
    return out
}

/**
 * 头像大小与缩放级别成正比：级别越高头像越大（zoom 16 ≈ 64px）。
 * 系数 4 可调，范围被钳制在 [16, 160]。
 */
private fun avatarSizeForZoom(zoom: Float): Int = (zoom * 4f).toInt().coerceIn(16, 160)

@Composable
private fun ZoomButton(text: String, onClick: () -> Unit) {
    FilledIconButton(
        onClick = onClick,
        colors = IconButtonDefaults.filledIconButtonColors(
            containerColor = MaterialTheme.colorScheme.surface,
            contentColor = MaterialTheme.colorScheme.onSurface
        )
    ) {
        Text(text = text, style = MaterialTheme.typography.titleLarge)
    }
}
