#pragma once

#include <cstdint>

namespace common {

// 自定义二进制协议头
// M0 阶段仅定义结构体占位; M1 实现 Codec 编解码 + 粘包/拆包处理
// 协议格式: 长度前缀(4) + 消息类型(2) + 序列号(4) + body(变长)
// 相比 JSON, 二进制协议显著压缩单条消息体积 (见设计方案)
#pragma pack(push, 1)
struct PacketHeader {
    uint32_t length;    // 数据体长度 (不含头部, 网络字节序)
    uint16_t msg_type;  // 消息类型
    uint32_t seq;       // 序列号, 用于请求/响应匹配与去重
};
#pragma pack(pop)

constexpr uint32_t kHeaderSize = sizeof(PacketHeader);  // 10 字节
constexpr uint32_t kMaxPacketSize = 1024 * 1024;        // 单包上限 1MB

}  // namespace common
