# logic — 业务逻辑节点 (C++ 宿主 + Lua 脚本)

启动 Lua 虚拟机，通过 gRPC 接收网关转发的业务消息，把 C++ 接口（发消息、查库）用 `sol2` 绑定给 Lua。

- `src/main.cpp` — 启动 Lua 虚拟机 + gRPC Server
- `src/lua_binder.cpp` — sol2 绑定
- `lua_scripts/` — 纯业务代码（`init.lua` / `aoi.lua` / `attendance.lua` / `chat.lua` / `social.lua`），支持热更新

**M1/M3 里程碑接入。**
