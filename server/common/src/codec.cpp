#include "codec.h"

#include <bit>
#include <cstdint>
#include <cstring>

namespace common {

namespace {

constexpr bool kLittleEndian = (std::endian::native == std::endian::little);

inline uint32_t swap32(uint32_t v) {
    return ((v & 0x000000ffu) << 24) | ((v & 0x0000ff00u) << 8) |
           ((v & 0x00ff0000u) >> 8) | ((v & 0xff000000u) >> 24);
}

inline uint16_t swap16(uint16_t v) {
    return static_cast<uint16_t>((v >> 8) | (v << 8));
}

// 网络字节序 = 大端，本机小端时需交换
inline uint32_t read_u32(const char* p) {
    uint32_t v;
    std::memcpy(&v, p, sizeof(v));
    return kLittleEndian ? swap32(v) : v;
}
inline uint16_t read_u16(const char* p) {
    uint16_t v;
    std::memcpy(&v, p, sizeof(v));
    return kLittleEndian ? swap16(v) : v;
}
inline void write_u32(char* p, uint32_t v) {
    if (kLittleEndian) {
        v = swap32(v);
    }
    std::memcpy(p, &v, sizeof(v));
}
inline void write_u16(char* p, uint16_t v) {
    if (kLittleEndian) {
        v = swap16(v);
    }
    std::memcpy(p, &v, sizeof(v));
}

}  // namespace

std::string encode_packet(uint16_t msg_type, uint32_t seq, const std::string& body) {
    std::string out;
    out.resize(kHeaderSize + body.size());
    char* p = out.data();
    write_u32(p, static_cast<uint32_t>(body.size()));
    write_u16(p + 4, msg_type);
    write_u32(p + 6, seq);
    if (!body.empty()) {
        std::memcpy(p + kHeaderSize, body.data(), body.size());
    }
    return out;
}

std::vector<Packet> PacketDecoder::feed(const char* data, std::size_t n) {
    buffer_.append(data, n);
    std::vector<Packet> packets;

    std::size_t pos = 0;
    while (!error_ && buffer_.size() - pos >= kHeaderSize) {
        const char* h = buffer_.data() + pos;
        uint32_t length = read_u32(h);
        if (length > kMaxPacketSize) {
            error_ = true;  // 长度越界，判定非法帧
            break;
        }
        std::size_t total = kHeaderSize + length;
        if (buffer_.size() - pos < total) {
            break;  // 半包，等更多数据
        }
        Packet pkt;
        pkt.msg_type = read_u16(h + 4);
        pkt.seq = read_u32(h + 6);
        pkt.body.assign(h + kHeaderSize, length);
        packets.push_back(std::move(pkt));
        pos += total;
    }

    if (pos > 0) {
        buffer_.erase(0, pos);  // 丢弃已消费部分
    }
    if (error_) {
        buffer_.clear();
    }
    return packets;
}

}  // namespace common
