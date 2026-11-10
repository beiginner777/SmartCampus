#!/usr/bin/env python3
"""M1 端到端测试客户端：注册 → 登录 → 心跳 → 异地登录互踢。

模拟 Android 客户端，用原生 socket 对接服务端二进制协议（大端）。
用法：python scripts/m1_client_test.py [host] [port]
"""
import json
import socket
import struct
import sys
import time

# 兼容 Windows GBK 控制台，避免 emoji/中文打印报错
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 9000

# 消息类型（与服务端 common/protocol.h 保持一致）
REGISTER_REQ = 0x0001
LOGIN_REQ = 0x0002
HEARTBEAT_REQ = 0x0003
KICK_NOTIFY = 0x8005
ERROR = 0x8000

MAX_PACKET = 1024 * 1024


def encode(msg_type, seq, body: bytes) -> bytes:
    return struct.pack(">IHI", len(body), msg_type, seq) + body


def decode_stream(buf: bytes):
    """从字节流切出完整包（粘包/拆包），返回 (packets, 剩余缓冲)"""
    packets = []
    while len(buf) >= 10:
        length, msg_type, seq = struct.unpack(">IHI", buf[:10])
        if length > MAX_PACKET:
            raise ValueError("长度越界")
        if len(buf) < 10 + length:
            break
        packets.append((msg_type, seq, buf[10:10 + length]))
        buf = buf[10 + length:]
    return packets, buf


class Conn:
    def __init__(self):
        self.sock = socket.create_connection((HOST, PORT), timeout=5)
        self.buf = b""
        self.seq = 0

    def req(self, msg_type, obj, timeout=5):
        self.seq += 1
        self.sock.sendall(encode(msg_type, self.seq, json.dumps(obj).encode()))
        return self._wait(self.seq, timeout)

    def read_any(self, timeout=5):
        """读取任意一个包（用于等待 KICK 推送）"""
        self.sock.settimeout(timeout)
        while True:
            packets, self.buf = decode_stream(self.buf)
            if packets:
                return packets[0]
            data = self.sock.recv(4096)
            if not data:
                return None  # EOF（对端关闭）
            self.buf += data

    def _wait(self, seq, timeout):
        self.sock.settimeout(timeout)
        deadline = time.time() + timeout
        while time.time() < deadline:
            packets, self.buf = decode_stream(self.buf)
            for i, pkt in enumerate(packets):
                if pkt[1] == seq:
                    return pkt
            try:
                data = self.sock.recv(4096)
            except socket.timeout:
                continue
            if not data:
                raise ConnectionError("连接关闭")
            self.buf += data
        raise TimeoutError("等待响应超时")

    def close(self):
        self.sock.close()


def main():
    username = f"test_{int(time.time())}"
    password = "123456"

    c1 = Conn()

    mt, seq, body = c1.req(REGISTER_REQ,
                           {"username": username, "password": password, "role": 0,
                            "nickname": "测试用户", "tags": "跑步,篮球"})
    print("[1] 注册响应:", json.loads(body))

    mt, seq, body = c1.req(LOGIN_REQ, {"username": username, "password": password})
    login = json.loads(body)
    print("[2] 登录响应:", login)
    assert login["code"] == 0, f"登录失败: {login}"
    token = login["token"]
    print("    拿到 token:", token[:40] + "...")

    mt, seq, body = c1.req(HEARTBEAT_REQ, {})
    print("[3] 心跳响应:", json.loads(body))

    # 第二台设备登录同账号
    c2 = Conn()
    mt, seq, body = c2.req(LOGIN_REQ, {"username": username, "password": password})
    login2 = json.loads(body)
    print("[4] 第二设备登录响应:", login2)
    assert login2["code"] == 0

    # 第一连接应收到 KICK 通知
    pkt = c1.read_any(timeout=5)
    if pkt is None:
        print("[5] [FAIL] 第一连接未收到 KICK，被直接断开")
        return 1
    kmt, kseq, kbody = pkt
    print("[5] 第一连接收到:", hex(kmt), json.loads(kbody))
    assert kmt == KICK_NOTIFY, f"期望 KICK_NOTIFY(0x8005)，实际 0x{kmt:04x}"
    print("    [PASS] 异地登录互踢验证通过")

    c1.close()
    c2.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
