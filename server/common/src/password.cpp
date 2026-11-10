#include "password.h"

#include "crypto.h"

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace common {

namespace {
constexpr uint32_t kIterations = 100000;  // PBKDF2 迭代次数
constexpr std::size_t kSaltLen = 16;
constexpr std::size_t kHashLen = 32;  // SHA256 输出长度

std::string random_bytes(std::size_t n) {
    std::random_device rd;
    std::string out(n, '\0');
    for (auto& c : out) {
        c = static_cast<char>(rd() & 0xff);
    }
    return out;
}
}  // namespace

std::string hash_password(const std::string& plain) {
    std::string salt = random_bytes(kSaltLen);
    std::string hash = pbkdf2_sha256(plain, salt, kIterations, kHashLen);
    return "pbkdf2_sha256$" + std::to_string(kIterations) + "$" + base64url_encode(salt) +
           "$" + base64url_encode(hash);
}

bool verify_password(const std::string& plain, const std::string& stored) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= stored.size(); ++i) {
        if (i == stored.size() || stored[i] == '$') {
            parts.push_back(stored.substr(start, i - start));
            start = i + 1;
        }
    }
    if (parts.size() != 4 || parts[0] != "pbkdf2_sha256") {
        return false;
    }

    uint32_t iterations = static_cast<uint32_t>(std::stoul(parts[1]));
    std::string salt = base64url_decode(parts[2]);
    std::string expected = base64url_decode(parts[3]);

    std::string actual = pbkdf2_sha256(plain, salt, iterations, expected.size());
    return constant_time_equals(actual, expected);
}

}  // namespace common
