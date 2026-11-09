package com.example.smartcampus.service

/**
 * 前台服务保活（M3 接入）。
 *
 * 职责：App 退后台时维持 TCP 长连接 + 告警通知；考勤中防强杀。
 * M0 仅占位，正式实现为 [android.app.Service] 子类（Foreground Service）。
 */
object KeepAliveService {
    // TODO(M3): 实现 Foreground Service 维持长连接 + 常驻通知
    // TODO(M3): 考勤中按返回键触发拦截弹窗；强杀后服务端心跳超时告警
}
