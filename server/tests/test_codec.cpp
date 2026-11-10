#include <gtest/gtest.h>

#include "codec.h"

using common::encode_packet;
using common::PacketDecoder;

TEST(CodecTest, Roundtrip) {
    auto bytes = encode_packet(0x0002, 42, "hello");
    PacketDecoder d;
    auto pkts = d.feed(bytes);
    ASSERT_EQ(pkts.size(), 1u);
    EXPECT_EQ(pkts[0].msg_type, 0x0002);
    EXPECT_EQ(pkts[0].seq, 42u);
    EXPECT_EQ(pkts[0].body, "hello");
    EXPECT_FALSE(d.has_error());
}

TEST(CodecTest, StickyPackets) {
    // 粘包：两个包连在一起，一次读入应切出两帧
    std::string bytes = encode_packet(1, 1, "a") + encode_packet(2, 2, "bb");
    PacketDecoder d;
    auto pkts = d.feed(bytes);
    ASSERT_EQ(pkts.size(), 2u);
    EXPECT_EQ(pkts[0].msg_type, 1);
    EXPECT_EQ(pkts[1].msg_type, 2);
    EXPECT_EQ(pkts[0].body, "a");
    EXPECT_EQ(pkts[1].body, "bb");
}

TEST(CodecTest, FragmentedPackets) {
    // 拆包：一个包分两次到达，第一次只有部分头部
    auto bytes = encode_packet(3, 7, std::string(100, 'x'));
    PacketDecoder d;
    EXPECT_TRUE(d.feed(bytes.substr(0, 5)).empty());
    auto pkts = d.feed(bytes.substr(5));
    ASSERT_EQ(pkts.size(), 1u);
    EXPECT_EQ(pkts[0].seq, 7u);
    EXPECT_EQ(pkts[0].body.size(), 100u);
}

TEST(CodecTest, OversizeRejected) {
    // 构造 length = 0x200000 (> 1MB 上限) 的头部，应触发错误
    char hdr[common::kHeaderSize] = {};
    hdr[0] = 0x00;
    hdr[1] = 0x20;
    hdr[2] = 0x00;
    hdr[3] = 0x00;  // length 大端 = 0x00200000
    hdr[4] = 0x00;
    hdr[5] = 0x01;  // msg_type
    PacketDecoder d;
    d.feed(std::string(hdr, common::kHeaderSize));
    EXPECT_TRUE(d.has_error());
}
