#pragma once

#include <string>

namespace common {

// 密码哈希：PBKDF2-HMAC-SHA256 + 随机盐（不存明文）
// 存储格式：pbkdf2_sha256$<iterations>$<salt_b64url>$<hash_b64url>
std::string hash_password(const std::string& plain);

// 校验密码，返回 true 表示匹配
bool verify_password(const std::string& plain, const std::string& stored);

}  // namespace common
