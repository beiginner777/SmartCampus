package com.example.smartcampus

import android.app.Application
import com.example.smartcampus.di.AppContainer

/**
 * 应用入口：持有全局依赖容器（手写 DI）。
 *
 * M0 采用手写 DI（非 Hilt），一个 Application 级容器集中创建并持有单例依赖，
 * 零注解处理器、依赖关系直观，后续依赖图变复杂可平滑迁移 Hilt。
 */
class SmartCampusApp : Application() {

    lateinit var container: AppContainer
        private set

    override fun onCreate() {
        super.onCreate()
        container = AppContainer(this)
    }
}
