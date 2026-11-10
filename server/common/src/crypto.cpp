#include "crypto.h"

#include <cstdint>
#include <vector>

#include "picosha2.h"

namespace common {

std::string sha256(const std::string& data) {
    std::vector<unsigned char> hash(32);
    picosha2::hash256(data.begin(), data.end(), hash.begin(), hash.end());
    return std::string(reinterpret_cast<const char*>(hash.data()), hash.size());
}

std::string hmac_sha256(const std::string& key, const std::string& msg) {
    constexpr std::size_t kBlockSize = 64;
    std::string k = key;
    if (k.size() > kBlockSize) {
        k = sha256(key);
    }
    k.resize(kBlockSize, '\0');

    std::string ipad(kBlockSize, '\0');
    std::string opad(kBlockSize, '\0');
    for (std::size_t i = 0; i < kBlockSize; ++i) {
        ipad[i] = static_cast<char>(static_cast<unsigned char>(k[i]) ^ 0x36);
        opad[i] = static_cast<char>(static_cast<unsigned char>(k[i]) ^ 0x5c);
    }
    return sha256(opad + sha256(ipad + msg));
}

namespace {
const char kBase64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}  // namespace

std::string base64url_encode(const std::string& data) {
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 3 <= data.size(); i += 3) {
        uint32_t n = (static_cast<uint32_t>(static_cast<unsigned char>(data[i])) << 16) |
                     (static_cast<uint32_t>(static_cast<unsigned char>(data[i + 1])) << 8) |
                     static_cast<uint32_t>(static_cast<unsigned char>(data[i + 2]));
        out += kBase64Chars[(n >> 18) & 63];
        out += kBase64Chars[(n >> 12) & 63];
        out += kBase64Chars[(n >> 6) & 63];
        out += kBase64Chars[n & 63];
    }
    std::size_t rem = data.size() - i;
    if (rem == 1) {
        uint32_t n = static_cast<uint32_t>(static_cast<unsigned char>(data[i])) << 16;
        out += kBase64Chars[(n >> 18) & 63];
        out += kBase64Chars[(n >> 12) & 63];
    } else if (rem == 2) {
        uint32_t n = (static_cast<uint32_t>(static_cast<unsigned char>(data[i])) << 16) |
                     (static_cast<uint32_t>(static_cast<unsigned char>(data[i + 1])) << 8);
        out += kBase64Chars[(n >> 18) & 63];
        out += kBase64Chars[(n >> 12) & 63];
        out += kBase64Chars[(n >> 6) & 63];
    }
    for (auto& c : out) {
        if (c == '+') {
            c = '-';
        } else if (c == '/') {
            c = '_';
        }
    }
    return out;
}

std::string base64url_decode(const std::string& input) {
    std::string s = input;
    for (auto& c : s) {
        if (c == '-') {
            c = '+';
        } else if (c == '_') {
            c = '/';
        }
    }
    while (s.size() % 4 != 0) {
        s += '=';
    }

    auto decode_char = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };

    std::string out;
    out.reserve(s.size() / 4 * 3);
    for (std::size_t i = 0; i < s.size(); i += 4) {
        int a = decode_char(s[i]);
        int b = decode_char(s[i + 1]);
        int c = (s[i + 2] == '=') ? 0 : decode_char(s[i + 2]);
        int d = (s[i + 3] == '=') ? 0 : decode_char(s[i + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0) {
            return "";
        }
        uint32_t n = (static_cast<uint32_t>(a) << 18) | (static_cast<uint32_t>(b) << 12) |
                     (static_cast<uint32_t>(c) << 6) | static_cast<uint32_t>(d);
        out += static_cast<char>((n >> 16) & 0xff);
        if (s[i + 2] != '=') {
            out += static_cast<char>((n >> 8) & 0xff);
        }
        if (s[i + 3] != '=') {
            out += static_cast<char>(n & 0xff);
        }
    }
    return out;
}

std::string pbkdf2_sha256(const std::string& password, const std::string& salt,
                          uint32_t iterations, std::size_t dklen) {
    std::string dk;
    dk.reserve(dklen);
    uint32_t block = 1;
    while (dk.size() < dklen) {
        std::string block_input = salt;
        block_input += static_cast<char>((block >> 24) & 0xff);
        block_input += static_cast<char>((block >> 16) & 0xff);
        block_input += static_cast<char>((block >> 8) & 0xff);
        block_input += static_cast<char>(block & 0xff);

        std::string u = hmac_sha256(password, block_input);
        std::string t = u;
        for (uint32_t i = 1; i < iterations; ++i) {
            u = hmac_sha256(password, u);
            for (std::size_t j = 0; j < t.size(); ++j) {
                t[j] = static_cast<char>(static_cast<unsigned char>(t[j]) ^
                                         static_cast<unsigned char>(u[j]));
            }
        }
        dk += t;
        ++block;
    }
    dk.resize(dklen);
    return dk;
}

bool constant_time_equals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
        return false;
    }
    unsigned char diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    }
    return diff == 0;
}

}  // namespace common
