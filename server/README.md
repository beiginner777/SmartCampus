# 服务端 (server)

智慧校园系统服务端，基于 C++20 + boost/asio 的高并发网关，业务逻辑下沉到 Lua（`sol2`）。

> 详见 `../README.md`（设计方案）

当前进度：**M0（EchoServer）+ M1（通信底座 + 鉴权）已完成**。

## 目录结构

```
server/
├── CMakeLists.txt      # 顶层 CMake，统一管理编译
├── config/server.json  # 运行时配置（监听 / 线程 / 日志 / jwt / postgres / redis）
├── common/             # 公共基础库 libcommon.a
│   ├── include/        # config / logger / protocol / codec / message_router
│   │                   # jwt_auth / password / crypto / pg_pool / redis_pool
│   └── src/            # 对应实现
├── gateway/            # 接入网关 (Asio 引擎)
│   ├── include/        # tcp_server / session / auth_service
│   └── src/            # main / tcp_server / session / auth_service
├── sql/schema.sql      # users 表 DDL (PostgreSQL)
├── scripts/            # m1_client_test.py 端到端测试客户端
├── proto/ logic/ registry/ auth/ resource/   # 预留目录（后续里程碑接入）
├── tests/              # 单元测试 (googletest)
└── third-party/        # jsoncpp vendored; picosha2; libpq 导入库; FetchContent(spdlog/gtest/hiredis)
```

## 技术栈

| 组件 | 库 | 说明 |
|---|---|---|
| 网络 | boost/asio | 跨平台异步 IO |
| 二进制协议 | 自研 Codec | 长度前缀 + msg_type + seq + body，粘包/拆包 |
| JWT | 自研（PicoSHA2 做 SHA256） | HS256，标准 header.payload.signature |
| 密码哈希 | PBKDF2-HMAC-SHA256 | 不存明文 |
| 数据库 | libpq（官方 C 客户端） | 手写 SQL + 连接池 |
| Redis | hiredis（官方 C 客户端） | 会话缓存 |
| JSON/日志 | jsoncpp / spdlog | |

## 环境依赖

- CMake ≥ 3.24、C++20 编译器（MinGW-w64 gcc 或 MSVC）
- Boost（仅头文件 `boost/asio`）— 通过 `-DBOOST_ROOT` 指定
- PostgreSQL（运行中，含 `libpq.dll`，默认 `D:/Program Files/PostgreSQL/17`）
- Redis（可选，缺失时互踢/会话降级）
- 首次构建需联网（spdlog / googletest / hiredis 走 FetchContent）

## 构建

```bash
cmake -S . -B build -G Ninja -DBOOST_ROOT="D:/Program Files/boost"
cmake --build build
```

> PostgreSQL 的 `lib/libpq.a` 是静态库（捆绑 pthread-win32，与 MinGW 冲突），
> 需先生成 `libpq.dll` 的 MinGW 导入库（见 `third-party/libpq/README.md`），否则 CMake 会报错。

## 运行

```bash
# 1. 建库建表（首次）
psql -U postgres -c "CREATE DATABASE smart_campus;"
psql -U postgres -d smart_campus -f sql/schema.sql

# 2. 启动 Redis（可选，会话/互踢缓存）
redis-server --port 6380

# 3. 把 PostgreSQL bin 加入 PATH（供 libpq.dll 加载），启动网关
PATH="/d/Program Files/PostgreSQL/17/bin:$PATH"
./build/gateway/gateway.exe config/server.json
```

> PostgreSQL 账号密码在 `config/server.json` 的 `postgres.password`（本地用，勿提交仓库）。

## 测试

```bash
# 单元测试（codec/jwt/config）
ctest --test-dir build --output-on-failure

# 端到端（注册→登录→心跳→异地互踢）
python scripts/m1_client_test.py
```

## M1 验收

注册 → 登录拿到 token → 心跳 → 第二台设备同账号登录 → 旧连接收到 `KICK_NOTIFY(0x8005)` 并被踢下线。
