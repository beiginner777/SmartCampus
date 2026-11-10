# 服务端 (server)

智慧校园系统服务端，基于 C++20 + boost/asio 的高并发网关，业务逻辑下沉到 Lua（`sol2`）。

> 详见 `../README.md`（设计方案）

当前进度：**M0（EchoServer）+ M1（通信底座 + 鉴权）+ gRPC 控制面（异地互踢）已完成**。

## 目录结构

```
server/
├── CMakeLists.txt      # 顶层 CMake，统一管理编译
├── config/server.json  # 运行时配置（监听 / 线程 / 日志 / jwt / postgres / redis / auth / gateway_grpc）
├── common/             # 公共基础库 libcommon.a
│   ├── include/        # config / logger / protocol / codec / message_router
│   │                   # jwt_auth / password / crypto / pg_pool / redis_pool
│   └── src/            # 对应实现
├── gateway/            # 接入网关 (Asio 引擎 + gRPC 控制面)
│   ├── include/        # tcp_server / session / auth_service / kick_service
│   └── src/            # main / tcp_server / session / auth_service / kick_service
├── auth/               # 认证微服务 (beast HTTP)
│   ├── include/        # auth_handler / http_server
│   └── src/            # main / auth_handler / http_server
├── proto/              # 网关控制面 gRPC 接口（gateway.proto + 生成代码）
├── sql/schema.sql      # users 表 DDL (PostgreSQL)
├── scripts/            # m1_client_test.py 端到端测试客户端
├── tests/              # 单元测试 (googletest)
└── third-party/        # jsoncpp/picosha2 vendored; FetchContent(spdlog/gtest/hiredis)
```

## 技术栈

| 组件 | 库 | 说明 |
|---|---|---|
| 网络 | boost/asio | 跨平台异步 IO |
| 二进制协议 | 自研 Codec | 长度前缀 + msg_type + seq + body，粘包/拆包 |
| JWT | 自研（PicoSHA2 做 SHA256） | HS256，标准 header.payload.signature |
| 密码哈希 | PBKDF2-HMAC-SHA256 | 不存明文 |
| 数据库 | libpq（官方 C 客户端） | 手写 SQL + 连接池 |
| Redis | hiredis（官方 C 客户端） | 会话缓存 + 路由表 `conn:{uid}` |
| HTTP | beast（boost 生态） | AuthServer 鉴权接口 |
| RPC | gRPC | 网关控制面 KickUser（异地互踢） |
| JSON/日志 | jsoncpp / spdlog | |

## 环境依赖

- CMake ≥ 3.24、**MSVC（Visual Studio 2022）**——gRPC 官方不支持 MinGW，故全项目用 MSVC
- Boost（仅头文件 `boost/asio`）— 通过 `-DBOOST_ROOT` 指定
- PostgreSQL（运行中，默认 `D:/Program Files/PostgreSQL/17`，链 `libpq.lib`）
- Redis（可选，缺失时互踢/会话降级）
- gRPC（已构建的 MSVC Debug 库，默认 `D:/Program Files/grpc`，见 `CMakeLists.txt` 的 `grpc_deps`）
- 首次构建需联网（spdlog / googletest / hiredis 走 FetchContent，已解压可跳过）

## 构建

```bash
# MSVC + Visual Studio 生成器（多配置工程，构建必须带 --config Debug）
cmake -S . -B build -G "Visual Studio 17 2022" \
  -DBOOST_ROOT="D:/Program Files/boost" \
  -DPOSTGRES_ROOT="D:/Program Files/PostgreSQL/17"
cmake --build build --config Debug
```

## 运行

```bash
# 1. 建库建表（首次）
psql -U postgres -c "CREATE DATABASE smart_campus;"
psql -U postgres -d smart_campus -f sql/schema.sql

# 2. 启动 Redis（会话 / 路由表缓存）
redis-server --port 6380

# 3. 把 PostgreSQL bin 加入 PATH（供 libpq.dll 加载），分别启动 AuthServer 和 Gateway
PATH="/d/Program Files/PostgreSQL/17/bin:$PATH"
./build/auth/Debug/auth.exe config/server.json        # AuthServer: HTTP 8080
./build/gateway/Debug/gateway.exe config/server.json  # Gateway: TCP 9000 + gRPC 9001
```

> PostgreSQL 账号密码在 `config/server.json` 的 `postgres.password`（本地开发使用）。

## 测试

```bash
# 单元测试（codec/jwt/config）
ctest --test-dir build -C Debug --output-on-failure

# 端到端（HTTP注册/登录 → TCP鉴权 → 心跳 → 异地互踢 via gRPC）
python scripts/m1_client_test.py
```

## M1 验收

注册 → 登录拿到 token → TCP 鉴权 → 心跳 → 第二台设备同账号登录 → 旧连接收到 `KICK_NOTIFY(0x8005)` 并被踢下线（AuthServer 查路由表 `conn:{uid}` → gRPC `KickUser` 精准踢）。
