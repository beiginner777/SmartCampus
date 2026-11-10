#!/usr/bin/env python3
"""M1 端到端测试：HTTP 注册/登录 → TCP 鉴权 → 心跳 → 异地登录互踢。

用法：python scripts/m1_client_test.py [gateway_host] [gateway_port] [auth_port]
"""
import json
import socket
import struct
import sys
import time
import urllib.request

GW_HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
GW_PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 9000
AUTH_PORT = int(sys.argv[3]) if len(sys.argv) > 3 else 8080

# TCP 消息类型（与服务端 common/protocol.h 一致）
AUTH_REQ = 0x0006
HEARTBEAT_REQ = 0x0003
KICK_NOTIFY = 0x8005

MAX_PACKET = 1024 * 1024


def http_post(path, obj):
    url = f"http://{GW_HOST}:{AUTH_PORT}{path}"
    data = json.dumps(obj).encode()
    req = urllib.request.Request(url, data=data, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=5) as resp:
        return json.loads(resp.read().decode())


def encode(msg_type, seq, body: bytes) -> bytes:
    return struct.pack(">IHI", len(body), msg_type, seq) + body


def decode_stream(buf: bytes):
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
        self.sock = socket.create_connection((GW_HOST, GW_PORT), timeout=5)
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
                return None  # EOF
            self.buf += data

    def _wait(self, seq, timeout):
        self.sock.settimeout(timeout)
        deadline = time.time() + timeout
        while time.time() < deadline:
            packets, self.buf = decode_stream(self.buf)
            for pkt in packets:
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

    resp = http_post("/register", {"username": username, "password": password, "role": 0,
                                   "nickname": "测试用户", "tags": "跑步,篮球"})
    print("[1] HTTP 注册响应:", resp)

    login = http_post("/login", {"username": username, "password": password})
    print("[2] HTTP 登录响应:", login)
    assert login["code"] == 0, f"登录失败: {login}"
    token = login["token"]
    print("    拿到 token:", token[:40] + "...")

    c1 = Conn()
    mt, seq, body = c1.req(AUTH_REQ, {"token": token})
    print("[3] TCP 鉴权响应:", json.loads(body))

    mt, seq, body = c1.req(HEARTBEAT_REQ, {})
    print("[4] 心跳响应:", json.loads(body))

    login2 = http_post("/login", {"username": username, "password": password})
    print("[5] 第二设备登录响应:", login2)
    assert login2["code"] == 0

    pkt = c1.read_any(timeout=5)
    if pkt is None:
        print("[6] [FAIL] 第一连接未收到 KICK，被直接断开")
        return 1
    kmt, kseq, kbody = pkt
    print("[6] 第一连接收到:", hex(kmt), json.loads(kbody))
    assert kmt == KICK_NOTIFY, f"期望 KICK_NOTIFY(0x8005)，实际 0x{kmt:04x}"
    print("    [PASS] 异地登录互踢验证通过")

    c1.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
