# 公共基础库 (libcommon.a)
add_library(common STATIC
    src/config.cpp
    src/logger.cpp)

target_include_directories(common PUBLIC include)

target_link_libraries(common PUBLIC
    jsoncpp
    spdlog::spdlog_header_only)
