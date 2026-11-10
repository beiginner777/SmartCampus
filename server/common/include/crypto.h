#pragma once

#include <cstdint>
#include <string>

namespace common {

// SHA256 摘要，返回 32 原始字节
std::string sha256(const std::string& data);

// HMAC-SHA256 (RFC 2104)
std::string hmac_sha256(const std::string& key, const std::string& msg);

// Base64URL 编解码（JWT 用：+→-，/→_，无 = 填充）
std::string base64url_encode(const std::string& data);
std::string base64url_decode(const std::string& data);

// PBKDF2-HMAC-SHA256 (RFC 8018)，返回 dklen 字节派生密钥
std::string pbkdf2_sha256(const std::string& password, const std::string& salt,
                          uint32_t iterations, std::size_t dklen);

// 恒定时间比较，防时序攻击
bool constant_time_equals(const std::string& a, const std::string& b);

}  // namespace common
