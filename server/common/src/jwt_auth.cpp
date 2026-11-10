#include "jwt_auth.h"

#include "crypto.h"

#include <json/json.h>

#include <ctime>
#include <string>
#include <utility>
#include <vector>

namespace common {

namespace {
const std::string kHeaderJson = R"({"alg":"HS256","typ":"JWT"})";

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == sep) {
            parts.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return parts;
}
}  // namespace

JwtAuth::JwtAuth(std::string secret) : secret_(std::move(secret)) {}

std::string JwtAuth::sign(uint32_t uid, const std::string& username, int64_t exp) const {
    Json::Value payload;
    payload["uid"] = static_cast<Json::Value::UInt>(uid);
    payload["username"] = username;
    payload["exp"] = static_cast<Json::Value::Int>(exp);  // jsoncpp 0.5.0 仅 32 位 int

    Json::FastWriter writer;
    std::string payload_json = writer.write(payload);
    // FastWriter 输出末尾带换行，去掉
    while (!payload_json.empty() &&
           (payload_json.back() == '\n' || payload_json.back() == '\r')) {
        payload_json.pop_back();
    }

    std::string header_b64 = base64url_encode(kHeaderJson);
    std::string payload_b64 = base64url_encode(payload_json);
    std::string signature =
        base64url_encode(hmac_sha256(secret_, header_b64 + "." + payload_b64));
    return header_b64 + "." + payload_b64 + "." + signature;
}

bool JwtAuth::verify(const std::string& token, JwtPayload& out) const {
    auto parts = split(token, '.');
    if (parts.size() != 3) {
        return false;
    }

    const std::string& header_b64 = parts[0];
    const std::string& payload_b64 = parts[1];
    const std::string& signature_b64 = parts[2];

    // 校验签名
    std::string expected =
        base64url_encode(hmac_sha256(secret_, header_b64 + "." + payload_b64));
    if (!constant_time_equals(expected, signature_b64)) {
        return false;
    }

    // 解析 payload
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(base64url_decode(payload_b64), root)) {
        return false;
    }

    out.uid = root.isMember("uid") ? root["uid"].asUInt() : 0;
    out.username = root.isMember("username") ? root["username"].asString() : "";
    out.exp = root.isMember("exp") ? static_cast<int64_t>(root["exp"].asInt()) : 0;

    if (out.exp > 0) {
        int64_t now = static_cast<int64_t>(std::time(nullptr));
        if (now >= out.exp) {
            return false;
        }
    }
    return true;
}

}  // namespace common
