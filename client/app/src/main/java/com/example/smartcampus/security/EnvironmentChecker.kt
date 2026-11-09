package com.example.smartcampus.security

/**
 * 设备环境校验（M3 接入）。
 *
 * 职责：检测开发者模式 / USB 调试，拦截低级模拟器与静态虚拟定位。
 * M0 仅占位，对应开发计划 M3「环境校验」风控项。
 */
object EnvironmentChecker {
    // TODO(M3): 检测开发者模式（Settings.Global.ADB_ENABLED / DEVELOPMENT_SETTINGS_ENABLED）
    // TODO(M3): 检测 USB 调试，拦截模拟器
}
