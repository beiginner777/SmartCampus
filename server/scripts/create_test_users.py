#!/usr/bin/env python3
"""创建测试用户（幂等：已存在则跳过）。

前置：AuthServer 已在 8080 运行。
用法：python scripts/create_test_users.py [host] [port]
"""
import json
import sys
import urllib.error
import urllib.request

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8080

# (username, password, role, nickname)  role: 0=student 1=teacher 2=manager
USERS = [
    ("student1", "123456", 0, "学生甲"),
    ("student2", "123456", 0, "学生乙"),
    ("teacher1", "123456", 1, "教师甲"),
    ("manager1", "123456", 2, "管理员"),
]

ROLE_NAME = {0: "student", 1: "teacher", 2: "manager"}


def register(username, password, role, nickname):
    url = f"http://{HOST}:{PORT}/register"
    body = json.dumps({"username": username, "password": password,
                       "role": role, "nickname": nickname}).encode()
    req = urllib.request.Request(url, data=body, headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return json.loads(r.read().decode())
    except urllib.error.HTTPError as e:
        return {"code": -1, "msg": f"HTTP {e.code}"}


def main():
    for username, password, role, nickname in USERS:
        r = register(username, password, role, nickname)
        code = r.get("code")
        if code == 0:
            print(f"[OK]   创建 {username} (role={ROLE_NAME[role]})")
        elif code == 2:
            print(f"[SKIP] {username} 已存在")
        else:
            print(f"[FAIL] {username}: {r}")

    print("\n===== 测试账号（密码均为 123456）=====")
    for username, password, role, nickname in USERS:
        print(f"  {username:<10} / {password:<8} / {ROLE_NAME[role]:<8} / {nickname}")


if __name__ == "__main__":
    main()
