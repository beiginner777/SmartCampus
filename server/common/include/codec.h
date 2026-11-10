#pragma once

#include "protocol.h"

#include <cstdint>
#include <string>
#include <vector>

namespace common {

// 一帧完整消息（解码后）
struct Packet {
    uint16_t msg_type = 0;
    uint32_t seq = 0;
    std::string body;
};

// 编码：长度前缀(4) + msg_type(2) + seq(4) + body → 字节流（网络字节序）
std::string encode_packet(uint16_t msg_type, uint32_t seq, const std::string& body);

// 增量解码器：喂入字节流，切出完整包（解决粘包/拆包）
class PacketDecoder {
public:
    std::vector<Packet> feed(const char* data, std::size_t n);
    std::vector<Packet> feed(const std::string& data) { return feed(data.data(), data.size()); }

    // 是否发生协议错误（length 非法/越界），此时应断开连接
    bool has_error() const { return error_; }

    void reset() {
        buffer_.clear();
        error_ = false;
    }

private:
    std::string buffer_;
    bool error_ = false;
};

}  // namespace common
