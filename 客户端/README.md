# 智慧校园系统 —— Android 客户端

> 《基于地理围栏与即时通讯的智慧校园系统》的 Android 客户端，使用 **Jetpack Compose** 构建。
> 后端为 C++（epoll 长连接 + AOI 九宫格索引）+ Lua（热更新业务规则）+ MySQL/Redis，本仓库只包含客户端。

## 目录结构

```
客户端/
├── app/                  # 客户端壳（占位首页，后续接入考勤/聊天等 MVP）
├── maptest/              # 地图开发沙盒（独立可运行，高德地图 + 缩放 + 头像 Marker）
├── gradle/               # Gradle wrapper + 版本目录（libs.versions.toml）
├── build.gradle.kts      # 根构建脚本
├── settings.gradle.kts   # 模块声明（:app + :maptest）
└── gradle.properties     # 含 android.overridePathCheck=true（中文路径需要）
```

- **`:app`** —— 正式客户端，后续所有业务功能在这里。
- **`:maptest`** —— 地图功能沙盒，AOI 聚合渲染、搜索高亮、地理围栏等地图相关 MVP 先在这里做，成熟后再迁回 `:app`。

## 环境要求

| 项 | 版本 |
|---|---|
| JDK | 21（Gradle 通过 foojay 工具链自动下载，见 `gradle/gradle-daemon-jvm.properties`） |
| Android SDK | compileSdk 36（platform 36.1） |
| Gradle | 9.4.1（使用 wrapper，无需本地安装） |
| minSdk / targetSdk | 28 / 36 |
| 地图 SDK | 高德 `com.amap.api:3dmap-location-search:11.3.100_loc11.3.101_sea9.8.1` |

## 快速开始

### 1. 克隆

```bash
git clone <仓库地址>
cd 客户端
```

### 2. 配置 local.properties

`local.properties` 已被 `.gitignore` 忽略，**clone 后需要自己新建**：

```properties
# 本机 Android SDK 路径（Windows 反斜杠要转义）
sdk.dir=D\:\\Android\\Sdk

# 高德地图 key（见下方"高德 key 申请"）
AMAP_KEY=你的高德key
```

> 默认 SDK 路径：Windows 一般为 `C:\Users\<用户名>\AppData\Local\Android\Sdk`。

### 3. 构建 / 运行

```bash
# 构建全部模块
./gradlew assembleDebug

# 只构建地图沙盒
./gradlew :maptest:assembleDebug

# 产物位置
maptest/build/outputs/apk/debug/maptest-debug.apk
```

用 Android Studio 打开本目录，运行配置选 **`maptest`** 即可单独跑地图测试。

## 高德地图 key 申请

地图需要高德 key 才能显示。key 绑定「包名 + 签名 SHA1」。

1. **查签名 SHA1**（两种方式任选）：
   ```bash
   # 方式一：命令行
   keytool -list -v -keystore ~/.android/debug.keystore -storepass android | grep -i sha1

   # 方式二：Gradle 官方报告（最直观，直接给 SHA1/SHA256/MD5）
   ./gradlew :maptest:signingReport
   ```

2. 到 [高德开放平台控制台](https://console.amap.com/) 创建应用 → 添加 key，填：
   - **平台**：Android 平台
   - **包名**：`com.example.smartcampus.maptest`
   - **SHA1（安全码）**：上一步查到的调试版 SHA1

3. 把生成的 key 填进 `local.properties` 的 `AMAP_KEY=`。

## ⚠️ 多端开发注意事项

**高德 key 绑定签名 SHA1，而 debug 签名的 keystore（`~/.android/debug.keystore`）是每台机器各自生成的**，所以不同机器的 SHA1 不一样。两台机器不能直接共用同一个 key，二选一：

- **各自注册 key**（简单）：每台机器用自己的 SHA1 + 相同的包名，到高德控制台各注册一个 key。
- **共用 keystore**（统一）：把其中一台机器的 `debug.keystore` 复制到另一台机器的 `~/.android/` 下，两边 SHA1 一致，共用一个 key。

> 注意：高德地图 **只支持 ARM 真机**（so 仅 arm64-v8a / armeabi-v7a），**不支持 x86_64 模拟器**。请用真机调试。

## 常见问题

| 现象 | 原因 | 解决 |
|---|---|---|
| 地图空白，logcat 报 `No implementation found ... nativeCreate` | 在 x86_64 模拟器上运行 | 用 ARM 真机 |
| 地图空白，logcat 报 `addMapEngine 10001` | key 鉴权失败（SHA1/包名/平台不对，或 SDK 版本过旧） | 核对 key 三要素；确认用的是组合包 `3dmap-location-search` |
| 地图空白，logcat 报 `errorCode 555570` | 未做隐私合规声明 | 用地图前先调 `MapsInitializer.updatePrivacyShow/Agree` |
| 构建报「path contains non-ASCII」 | 项目路径含中文 | 已在 `gradle.properties` 加 `android.overridePathCheck=true` |