# third-party — 第三方依赖

| 依赖        | 用途           | 获取方式                                    | 位置                          |
| ----------- | -------------- | ------------------------------------------- | ----------------------------- |
| boost/asio  | 网络库         | 本地已下载，通过 `-DBOOST_ROOT` 定位（体积过大不复制进仓库） | 外部 (如 `D:/Program Files/boost`) |
| jsoncpp     | 配置 JSON 解析 | 已 vendored 源码 (0.5.0)                    | `third-party/jsoncpp/`        |
| spdlog      | 日志           | FetchContent 自动下载                       | `third-party/_fetchcontent/`  |
| googletest  | 单元测试       | FetchContent 自动下载                       | `third-party/_fetchcontent/`  |

`third-party/_fetchcontent/` 为构建期自动生成的下载目录，已被 `.gitignore` 忽略。
