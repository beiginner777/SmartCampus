#!/usr/bin/env python3
"""登录脚本：模拟"第二台设备"完整登录 —— HTTP 登录拿 token → TCP 携带 token 鉴权，
用于真机测试异地互踢（踢号）。

用法：python scripts/login.py [username] [password] [gateway_host] [gateway_port] [auth_host] [auth_port]
默认：student1 / 123456 / 127.0.0.1 / 9000 / 127.0.0.1 / 8080

踢号测试步骤：
  1. 真机（Android 客户端）用某账号登录并建立 TCP 长连接（在线，网关写路由表 conn:{uid}）；
  2. 在电脑上跑本脚本，用同一账号登录 + 建连；
  3. 本脚本 HTTP 登录时，AuthServer 读到 conn:{uid} → gRPC KickUser → 踢掉真机连接；
     随后本脚本再 TCP 携带 token 鉴权，成为新的"在线设备"。
"""
import json
import socket
import struct
import sys
import urllib.error
import urllib.request

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

AUTH_HOST = sys.argv[5] if len(sys.argv) > 5 else "127.0.0.1"
AUTH_PORT = int(sys.argv[6]) if len(sys.argv) > 6 else 8080
GW_HOST = sys.argv[3] if len(sys.argv) > 3 else "127.0.0.1"
GW_PORT = int(sys.argv[4]) if len(sys.argv) > 4 else 9000

AUTH_REQ = 0x0006  # 与服务端 common/protocol.h 一致


def http_login(username, password):
    url = f"http://{AUTH_HOST}:{AUTH_PORT}/login"
    body = json.dumps({"username": username, "password": password}).encode()
    req = urllib.request.Request(url, data=body, headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return json.loads(r.read().decode())
    except urllib.error.HTTPError as e:
        return {"code": -1, "msg": f"HTTP {e.code}"}


def encode(msg_type, seq, body: bytes) -> bytes:
    return struct.pack(">IHI", len(body), msg_type, seq) + body


def decode_stream(buf: bytes):
    out = []
    while len(buf) >= 10:
        length, msg_type, seq = struct.unpack(">IHI", buf[:10])
        if length > 1024 * 1024:
            raise ValueError("长度越界")
        if len(buf) < 10 + length:
            break
        out.append((msg_type, seq, buf[10:10 + length]))
        buf = buf[10 + length:]
    return out, buf


def tcp_auth(token):
    """TCP 连网关，首包 AUTH_REQ 携带 token 鉴权，返回 AUTH_RESP"""
    s = socket.create_connection((GW_HOST, GW_PORT), timeout=5)
    s.sendall(encode(AUTH_REQ, 1, json.dumps({"token": token}).encode()))
    s.settimeout(30)
    buf = b""
    while True:
        packets, buf = decode_stream(buf)
        for mt, seq, b in packets:
            if seq == 1:
                return json.loads(b)
        data = s.recv(4096)
        if not data:
            return {"code": -1, "msg": "连接被关闭"}
        buf += data


def main():
    username = sys.argv[1] if len(sys.argv) > 1 else "student1"
    password = sys.argv[2] if len(sys.argv) > 2 else "123456"

    # 1) HTTP 登录拿 token
    r = http_login(username, password)
    if r.get("code") != 0:
        print(f"[FAIL] HTTP 登录失败: {r}")
        return 1
    token = r.get("token", "")
    print(f"[1] HTTP 登录成功 {username} (uid={r.get('uid')}, role={r.get('role')})")
    print(f"    token: {token[:60]}...")

    # 2) TCP 携带 token 鉴权
    a = tcp_auth(token)
    if a.get("code") == 0:
        print(f"[2] TCP 鉴权成功 uid={a.get('uid')} username={a.get('username')}")
    else:
        print(f"[2] TCP 鉴权失败: {a}")
        return 1

    print("    完成：已模拟第二台设备登录并建连，真机此刻应已被踢下线。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
