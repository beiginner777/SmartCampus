#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

namespace common {

// 消息路由：msg_type → handler 的注册/分发（header-only 模板）。
// ConnectionT 由使用方提供（gateway 里是 Session），handler 签名：
//   void(ConnectionT& conn, uint32_t seq, const std::string& body)
template <typename ConnectionT>
class MessageRouter {
public:
    using Handler = std::function<void(ConnectionT&, uint32_t, const std::string&)>;

    void register_handler(uint16_t msg_type, Handler handler) {
        handlers_[msg_type] = std::move(handler);
    }

    // 返回 false 表示没有注册对应的 handler
    bool dispatch(uint16_t msg_type, ConnectionT& conn, uint32_t seq, const std::string& body) {
        auto it = handlers_.find(msg_type);
        if (it == handlers_.end()) {
            return false;
        }
        it->second(conn, seq, body);
        return true;
    }

private:
    std::unordered_map<uint16_t, Handler> handlers_;
};

}  // namespace common
