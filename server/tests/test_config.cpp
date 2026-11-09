#include <gtest/gtest.h>

#include "config.h"

using common::Config;

TEST(ConfigTest, Defaults) {
    Config c = Config::from_json("{}");
    EXPECT_EQ(c.host, "0.0.0.0");
    EXPECT_EQ(c.port, 9000);
    EXPECT_EQ(c.thread_count, 4);
    EXPECT_EQ(c.log_level, "info");
}

TEST(ConfigTest, ParseFields) {
    Config c = Config::from_json(R"({
        "host": "127.0.0.1",
        "port": 12345,
        "thread_count": 8,
        "log_level": "debug",
        "log_file": "logs/x.log"
    })");
    EXPECT_EQ(c.host, "127.0.0.1");
    EXPECT_EQ(c.port, 12345);
    EXPECT_EQ(c.thread_count, 8);
    EXPECT_EQ(c.log_level, "debug");
    EXPECT_EQ(c.log_file, "logs/x.log");
}

TEST(ConfigTest, InvalidJsonThrows) {
    EXPECT_THROW(Config::from_json("{ invalid json"), std::runtime_error);
}
