#pragma once

#include <cstdint>
#include <string>

namespace common {

struct JwtPayload {
    uint32_t uid = 0;      // 用户 id（M1 阶段 32 位足够）
    std::string username;
    std::string nickname;  // 昵称（M2 地图广播 USER_ENTER/nearby 展示用）
    int64_t exp = 0;       // 过期时间 Unix 秒，0 表示不校验
};

// JWT HS256 无状态鉴权
class JwtAuth {
public:
    explicit JwtAuth(std::string secret);

    // 签发 token（header.payload.signature）；nickname 带默认值，向后兼容旧调用
    std::string sign(uint32_t uid, const std::string& username, int64_t exp,
                     const std::string& nickname = "") const;

    // 校验签名与过期，成功返回 true 并填充 payload
    bool verify(const std::string& token, JwtPayload& out) const;

private:
    std::string secret_;
};

}  // namespace common
