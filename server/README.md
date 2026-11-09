# 服务端 (server)

智慧校园系统服务端，基于 C++20 + boost/asio 的高并发网关，业务逻辑下沉到 Lua（`sol2`）。

> 详见 `../README.md`（设计方案）

## 目录结构

```
server/
├── CMakeLists.txt      # 顶层 CMake，统一管理编译
├── config/server.json  # 运行时配置（监听端口 / 线程数 / 日志）
├── common/             # C++ 公共基础库 (libcommon.a)：config / logger / protocol
├── gateway/            # 接入网关 (Asio 引擎)：TCP EchoServer (M0)
├── proto/              # 内部通信协议定义 (M1 接入)
├── logic/              # 业务逻辑节点 (C++ 宿主 + Lua, M1/M3 接入)
├── registry/           # 注册中心 (M1 接入)
├── auth/               # 认证微服务 (M1 接入)
├── resource/           # 文件与资源服务 (M3 预留)
├── tests/              # 单元测试 (googletest)
└── third-party/        # 第三方依赖 (jsoncpp vendored; spdlog/gtest FetchContent)
```

## 环境依赖

- CMake ≥ 3.24
- C++20 编译器（MinGW-w64 gcc 或 MSVC）
- Boost（仅需头文件，`boost/asio`）— 通过 `-DBOOST_ROOT` 指定
- 首次构建需联网（spdlog / googletest 通过 FetchContent 下载）

## 构建

```bash
# 在 server 目录下
cmake -S . -B build -G Ninja -DBOOST_ROOT="D:/Program Files/boost"
cmake --build build
```

> 用 `-DBOOST_ROOT=<你的 boost 根目录>` 指定BOOST库路径。

## 运行

```bash
.\build\gateway\gateway.exe config/server.json
```

启动后监听 `0.0.0.0:9000`，收到数据原样回显。

## 测试

```bash
ctest --test-dir build --output-on-failure
```

## M0 验收

服务端启动 → 客户端连上 → 发消息回显 → 断线打印日志。可用 `nc` / `telnet`
