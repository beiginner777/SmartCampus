package com.example.smartcampus.network.protocol

import java.nio.ByteBuffer
import java.nio.ByteOrder

/** 包头固定 10 字节：length(u32) + msg_type(u16) + seq(u32)，均为大端（网络字节序） */
const val HEADER_SIZE = 10

/** 单包上限 1MB（与服务端 kMaxPacketSize 一致），超出判定协议错误 */
const val MAX_PACKET_SIZE = 1024 * 1024

/**
 * 解码后的一帧完整消息（对应服务端 `common::Packet`）。
 */
class Packet(
    val msgType: Int,
    val seq: Int,
    val body: ByteArray,
)

/**
 * 编码一帧：length(4, 大端) + msg_type(2, 大端) + seq(4, 大端) + body。
 *
 * `length` 为 body 字节数（不含 10 字节头），与服务端 `encode_packet` 一致。
 */
fun encodePacket(msgType: Int, seq: Int, body: ByteArray): ByteArray {
    val buf = ByteBuffer.allocate(HEADER_SIZE + body.size).order(ByteOrder.BIG_ENDIAN)
    buf.putInt(body.size)
    buf.putShort(msgType.toShort())
    buf.putInt(seq)
    buf.put(body)
    return buf.array()
}

/**
 * 增量解码器：喂入字节流，切出完整包（解决粘包/拆包）。
 *
 * 与服务端 `PacketDecoder` 语义一致：
 *  - 半包：不足一帧时缓存，等更多数据；
 *  - 粘包：一次喂入多帧时全部切出；
 *  - `length` 非法/越界：置 [hasError]，上层应断开连接。
 */
class PacketDecoder {

    private var buffer = ByteArray(0)

    var hasError = false
        private set

    /**
     * 喂入一段字节（[length] 为实际有效长度，用于复用固定大小的读缓冲），返回切出的完整包。
     */
    fun feed(data: ByteArray, length: Int = data.size): List<Packet> {
        buffer = buffer + data.copyOf(length)
        val packets = mutableListOf<Packet>()
        var pos = 0

        while (!hasError && buffer.size - pos >= HEADER_SIZE) {
            val buf = ByteBuffer.wrap(buffer, pos, buffer.size - pos).order(ByteOrder.BIG_ENDIAN)
            val bodyLen = buf.getInt()
            if (bodyLen < 0 || bodyLen > MAX_PACKET_SIZE) {
                hasError = true  // 长度非法/越界
                break
            }
            val total = HEADER_SIZE + bodyLen
            if (buffer.size - pos < total) {
                break  // 半包，等更多数据
            }
            val msgType = buf.getShort().toInt() and 0xFFFF
            val seq = buf.getInt()
            val body = buffer.copyOfRange(pos + HEADER_SIZE, pos + total)
            packets.add(Packet(msgType, seq, body))
            pos += total
        }

        if (pos > 0) {
            buffer = buffer.copyOfRange(pos, buffer.size)  // 丢弃已消费部分
        }
        if (hasError) {
            buffer = ByteArray(0)
        }
        return packets
    }

    fun reset() {
        buffer = ByteArray(0)
        hasError = false
    }
}
