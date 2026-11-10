# third-party — 第三方依赖

| 依赖        | 用途           | 获取方式                                    | 位置                          |
| ----------- | -------------- | ------------------------------------------- | ----------------------------- |
| boost/asio  | 网络库         | 本地已下载，通过 `-DBOOST_ROOT` 定位（体积过大不复制进仓库） | 外部 (如 `D:/Program Files/boost`) |
| jsoncpp     | 配置/消息 JSON 解析 | 已 vendored 源码 (0.5.0)                | `third-party/jsoncpp/`        |
| picosha2    | SHA256（JWT/PBKDF2 底层） | 已 vendored 单头文件                | `third-party/picosha2.h`      |
| libpq       | PostgreSQL 客户端 | 本地已安装，`libpq.dll` 的 MinGW 导入库 | `third-party/libpq/`（生成，见其 README） |
| spdlog      | 日志           | FetchContent 自动下载                       | `third-party/_fetchcontent/`  |
| googletest  | 单元测试       | FetchContent 自动下载                       | `third-party/_fetchcontent/`  |
| hiredis     | Redis 客户端   | FetchContent 自动下载                       | `third-party/_fetchcontent/`  |

`third-party/_fetchcontent/` 与 `third-party/libpq/` 为构建期产物，已被 `.gitignore` 忽略。
