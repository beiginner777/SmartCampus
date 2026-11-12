#!/usr/bin/env python3
"""M2 AOI 验收模拟脚本：模拟多个"附近用户"，供一台真机在地图上看到实时位置。

原理（M2 单网关内广播）：
  - 脚本创建/登录 N 个 bot 用户，各自 HTTP 登录 + TCP 鉴权 + 周期上报坐标；
  - bot 散布在中心坐标周围（几十到几百米），并随机小步移动；
  - 真机客户端登录并上报到同一区域后，会收到这些 bot 的 USER_ENTER / POSITION_UPDATE，
    地图上即可看到它们实时移动；bot 侧也会打印看到真机进入/离开视野（验证双向广播）。

用法：
  python scripts/m2_aoi_sim.py [center_lat] [center_lng] [n_bots] [gw_host] [gw_port] [auth_port]

默认：center=(39.9500, 116.3000)、n_bots=8、网关 127.0.0.1:9000、Auth 8080
（把 center 改成真机实际所在坐标，或真机定位到 center 附近，二者在同一片 300m 邻域即可）

验收步骤：
  1. 启动 AuthServer（8080）与 Gateway（9000）；
  2. 跑本脚本（模拟 8 个附近用户，持续在线随机走动）；
  3. 真机登录，把定位设到 center 附近并上报坐标 → 地图出现 bot 头像；
  4. 观察：真机移动跨网格时，bot 控制台打印 USER_ENTER/USER_LEAVE（AOI 进入/离开语义）。
"""
import json
import math
import random
import socket
import struct
import sys
import threading
import time
import urllib.error
import urllib.request

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

CENTER_LAT = float(sys.argv[1]) if len(sys.argv) > 1 else 39.9500
CENTER_LNG = float(sys.argv[2]) if len(sys.argv) > 2 else 116.3000
N_BOTS = int(sys.argv[3]) if len(sys.argv) > 3 else 8
GW_HOST = sys.argv[4] if len(sys.argv) > 4 else "127.0.0.1"
GW_PORT = int(sys.argv[5]) if len(sys.argv) > 5 else 9000
AUTH_PORT = int(sys.argv[6]) if len(sys.argv) > 6 else 8080

# 与服务端 common/protocol.h 一致
AUTH_REQ = 0x0006
HEARTBEAT_REQ = 0x0003
POSITION_REPORT_REQ = 0x0007
AUTH_RESP = 0x8006
POSITION_REPORT_RESP = 0x8007
USER_ENTER = 0x8008
USER_LEAVE = 0x8009
POSITION_UPDATE = 0x800A

REPORT_INTERVAL = 3.0   # 坐标上报周期（秒）
HB_INTERVAL = 30.0      # 心跳周期（秒），与网关 timeout=90s 配合保活
MAX_PACKET = 1024 * 1024

stop = threading.Event()


def http_post(path, obj):
    url = f"http://{GW_HOST}:{AUTH_PORT}{path}"
    data = json.dumps(obj).encode()
    req = urllib.request.Request(url, data=data, headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return json.loads(r.read().decode())
    except urllib.error.HTTPError as e:
        return {"code": -1, "msg": f"HTTP {e.code}"}


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


def offset_meters(lat, lng, dx_m, dy_m):
    """以 (lat, lng) 为原点，偏移 (dx_m 东向, dy_m 北向) 米，返回 (lat, lng)。"""
    dlat = dy_m / 111320.0
    dlng = dx_m / (111320.0 * math.cos(math.radians(lat)))
    return lat + dlat, lng + dlng


class Bot(threading.Thread):
    def __init__(self, idx):
        super().__init__(daemon=True)
        self.idx = idx
        self.username = f"bot_{idx}"
        self.nickname = f"路人{idx}"
        # 初始位置：中心周围随机方向、半径 30~250m
        ang = random.uniform(0, 2 * math.pi)
        r = random.uniform(30, 250)
        self.lat, self.lng = offset_meters(CENTER_LAT, CENTER_LNG, r * math.cos(ang), r * math.sin(ang))
        self.sock = None
        self.buf = b""
        self.seq = 1

    def run(self):
        http_post("/register", {"username": self.username, "password": "123456",
                                "role": 0, "nickname": self.nickname})
        login = http_post("/login", {"username": self.username, "password": "123456"})
        if login.get("code") != 0:
            print(f"[{self.nickname}] 登录失败: {login}")
            return
        token = login.get("token", "")

        try:
            self.sock = socket.create_connection((GW_HOST, GW_PORT), timeout=10)
        except OSError as e:
            print(f"[{self.nickname}] 连网关失败: {e}")
            return

        self.sock.sendall(encode(AUTH_REQ, self.seq, json.dumps({"token": token}).encode()))
        self.seq += 1
        self.sock.settimeout(0.3)

        last_report = 0.0
        last_hb = time.time()
        print(f"[{self.nickname}] 上线，初始位置 ({self.lat:.6f}, {self.lng:.6f})")

        while not stop.is_set():
            # 收包（短超时轮询），处理推送
            try:
                data = self.sock.recv(4096)
                if not data:
                    break  # 对端关闭
                self.buf += data
            except socket.timeout:
                pass
            except OSError:
                break

            try:
                packets, self.buf = decode_stream(self.buf)
            except ValueError:
                break
            for mt, seq, body in packets:
                self.on_packet(mt, body)

            now = time.time()
            if now - last_report >= REPORT_INTERVAL:
                self.random_walk()
                body = json.dumps({"lng": self.lng, "lat": self.lat}).encode()
                self.sock.sendall(encode(POSITION_REPORT_REQ, self.seq, body))
                self.seq += 1
                last_report = now
            if now - last_hb >= HB_INTERVAL:
                self.sock.sendall(encode(HEARTBEAT_REQ, self.seq, b"{}"))
                self.seq += 1
                last_hb = now

        if self.sock:
            self.sock.close()
        print(f"[{self.nickname}] 下线")

    def random_walk(self):
        """每次上报小步随机移动（±25m），模拟缓慢走动。"""
        dx = random.uniform(-25, 25)
        dy = random.uniform(-25, 25)
        self.lat, self.lng = offset_meters(self.lat, self.lng, dx, dy)

    def on_packet(self, mt, body):
        try:
            obj = json.loads(body)
        except ValueError:
            return
        if mt == USER_ENTER:
            who = obj.get("nickname") or obj.get("uid")
            print(f"  ◀ [{self.nickname}] 看到 {who} 进入视野")
        elif mt == USER_LEAVE:
            print(f"  ◀ [{self.nickname}] {obj.get('uid')} 离开视野")
        elif mt == POSITION_REPORT_RESP and obj.get("nearby"):
            print(f"  ◀ [{self.nickname}] 附近有 {len(obj['nearby'])} 个用户")
        # POSITION_UPDATE 太频繁，不打印；AUTH_RESP / 心跳响应忽略


def main():
    print("=" * 60)
    print("M2 AOI 验收模拟：附近用户模拟器")
    print(f"中心坐标 ({CENTER_LAT}, {CENTER_LNG})   bot 数量 {N_BOTS}")
    print(f"网关 {GW_HOST}:{GW_PORT}   Auth {AUTH_PORT}")
    print("=" * 60)
    print("提示：真机请将定位设到中心坐标附近（±300m）再登录上报，")
    print("      即可在地图上看到这些 bot 实时移动。Ctrl+C 结束。\n")

    bots = [Bot(i) for i in range(1, N_BOTS + 1)]
    for b in bots:
        b.start()

    try:
        while True:
            time.sleep(0.5)
    except KeyboardInterrupt:
        print("\n收到 Ctrl+C，正在让所有 bot 下线…")
    finally:
        stop.set()
        for b in bots:
            b.join(timeout=2)
        print("已全部下线。")


if __name__ == "__main__":
    main()
