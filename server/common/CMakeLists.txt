# 公共基础库 (libcommon.a)
add_library(common STATIC
    src/config.cpp
    src/logger.cpp
    src/codec.cpp
    src/crypto.cpp
    src/jwt_auth.cpp
    src/password.cpp
    src/pg_pool.cpp
    src/redis_pool.cpp)

target_include_directories(common PUBLIC include)
target_include_directories(common PRIVATE ${CMAKE_SOURCE_DIR}/third-party)

target_link_libraries(common PUBLIC
    jsoncpp
    spdlog::spdlog_header_only)
target_link_libraries(common PRIVATE
    postgres_libpq
    hiredis)
