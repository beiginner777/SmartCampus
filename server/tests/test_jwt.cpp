#include <gtest/gtest.h>

#include <ctime>

#include "crypto.h"
#include "jwt_auth.h"
#include "password.h"

using common::JwtAuth;
using common::JwtPayload;

TEST(CryptoTest, Sha256Length) {
    EXPECT_EQ(common::sha256("").size(), 32u);
    EXPECT_EQ(common::sha256("abc").size(), 32u);
}

TEST(CryptoTest, HmacDeterministic) {
    EXPECT_EQ(common::hmac_sha256("key", "msg"), common::hmac_sha256("key", "msg"));
    EXPECT_NE(common::hmac_sha256("key", "msg"), common::hmac_sha256("key2", "msg"));
}

TEST(CryptoTest, Base64UrlRoundtrip) {
    std::string data = "hello world\x01\x02\x03";
    EXPECT_EQ(common::base64url_decode(common::base64url_encode(data)), data);
}

TEST(JwtTest, SignVerifyRoundtrip) {
    JwtAuth auth("my-secret");
    auto token = auth.sign(123, "alice", std::time(nullptr) + 3600);
    JwtPayload p;
    ASSERT_TRUE(auth.verify(token, p));
    EXPECT_EQ(p.uid, 123u);
    EXPECT_EQ(p.username, "alice");
}

TEST(JwtTest, TamperRejected) {
    JwtAuth auth("my-secret");
    auto token = auth.sign(1, "bob", std::time(nullptr) + 3600);
    auto dot = token.find('.');
    std::string tampered = token;
    tampered[dot + 1] = (tampered[dot + 1] == 'A') ? 'B' : 'A';
    JwtPayload p;
    EXPECT_FALSE(auth.verify(tampered, p));
}

TEST(JwtTest, ExpiredRejected) {
    JwtAuth auth("my-secret");
    auto token = auth.sign(1, "bob", std::time(nullptr) - 10);  // 已过期
    JwtPayload p;
    EXPECT_FALSE(auth.verify(token, p));
}

TEST(JwtTest, WrongSecretRejected) {
    JwtAuth a("secret-a");
    JwtAuth b("secret-b");
    auto token = a.sign(1, "bob", std::time(nullptr) + 3600);
    JwtPayload p;
    EXPECT_FALSE(b.verify(token, p));
}

TEST(PasswordTest, HashAndVerify) {
    std::string stored = common::hash_password("password123");
    EXPECT_TRUE(common::verify_password("password123", stored));
    EXPECT_FALSE(common::verify_password("wrong", stored));
}

TEST(PasswordTest, SaltIsRandom) {
    std::string a = common::hash_password("same");
    std::string b = common::hash_password("same");
    EXPECT_NE(a, b);  // 盐不同导致哈希不同
}
