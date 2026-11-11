package com.example.smartcampus.network.protocol

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * 二进制编解码单测：对照服务端 `test_codec.cpp` 的用例。
 */
class CodecTest {

    @Test
    fun encodeThenDecodeRoundtrip() {
        val body = "{\"token\":\"abc\"}".toByteArray(Charsets.UTF_8)
        val bytes = encodePacket(MsgType.AUTH_REQ, 7, body)
        assertEquals(HEADER_SIZE + body.size, bytes.size)

        val decoder = PacketDecoder()
        val packets = decoder.feed(bytes)
        assertFalse(decoder.hasError)
        assertEquals(1, packets.size)
        assertEquals(MsgType.AUTH_REQ, packets[0].msgType)
        assertEquals(7, packets[0].seq)
        assertArrayEquals(body, packets[0].body)
    }

    @Test
    fun bigEndianHeaderMatchesServer() {
        // 手工推演大端布局：length=3, msgType=0x0006, seq=0x01020304
        val bytes = encodePacket(0x0006, 0x01020304, byteArrayOf(1, 2, 3))
        assertEquals(HEADER_SIZE + 3, bytes.size)
        assertArrayEquals(byteArrayOf(0, 0, 0, 3), bytes.copyOfRange(0, 4))
        assertArrayEquals(byteArrayOf(0, 6), bytes.copyOfRange(4, 6))
        assertArrayEquals(byteArrayOf(1, 2, 3, 4), bytes.copyOfRange(6, 10))
    }

    @Test
    fun stickyPackets_splitMultipleFrames() {
        // 粘包：两帧一次读入
        val b1 = encodePacket(MsgType.HEARTBEAT_REQ, 1, byteArrayOf())
        val b2 = encodePacket(MsgType.HEARTBEAT_REQ, 2, byteArrayOf())
        val decoder = PacketDecoder()
        val out = decoder.feed(b1 + b2)
        assertEquals(2, out.size)
        assertEquals(1, out[0].seq)
        assertEquals(2, out[1].seq)
    }

    @Test
    fun fragmentedPacket_reassembles() {
        // 拆包：一帧分两次读入
        val full = encodePacket(MsgType.AUTH_REQ, 9, "hello".toByteArray(Charsets.UTF_8))
        val decoder = PacketDecoder()

        val first = decoder.feed(full.copyOfRange(0, 5))
        assertTrue(first.isEmpty())  // 半包头，未出包

        val second = decoder.feed(full.copyOfRange(5, full.size))
        assertEquals(1, second.size)
        assertEquals("hello", String(second[0].body, Charsets.UTF_8))
    }

    @Test
    fun oversizedLength_setsError() {
        // 声明 length 超过 1MB 上限 → 协议错误
        val header = ByteBuffer.allocate(HEADER_SIZE).order(ByteOrder.BIG_ENDIAN)
        header.putInt(MAX_PACKET_SIZE + 1)
        header.putShort(MsgType.AUTH_REQ.toShort())
        header.putInt(1)

        val decoder = PacketDecoder()
        decoder.feed(header.array())
        assertTrue(decoder.hasError)
    }

    @Test
    fun emptyBody_packet() {
        val bytes = encodePacket(MsgType.LOGOUT_REQ, 5, byteArrayOf())
        val packets = PacketDecoder().feed(bytes)
        assertEquals(1, packets.size)
        assertEquals(0, packets[0].body.size)
    }
}
