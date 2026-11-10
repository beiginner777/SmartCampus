#pragma once

#include <cstdint>
#include <string>

namespace common {

struct JwtPayload {
    uint32_t uid = 0;      // 用户 id（M1 阶段 32 位足够）
    std::string username;
    int64_t exp = 0;       // 过期时间 Unix 秒，0 表示不校验
};

// JWT HS256 无状态鉴权
class JwtAuth {
public:
    explicit JwtAuth(std::string secret);

    // 签发 token（header.payload.signature）
    std::string sign(uint32_t uid, const std::string& username, int64_t exp) const;

    // 校验签名与过期，成功返回 true 并填充 payload
    bool verify(const std::string& token, JwtPayload& out) const;

private:
    std::string secret_;
};

}  // namespace common
