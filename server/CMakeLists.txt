cmake_minimum_required(VERSION 3.24)

project(smart_campus_server
        VERSION 0.1.0
        DESCRIPTION "智慧校园系统服务端 (C++ Gateway + Lua Logic)"
        LANGUAGES CXX)

# ---------------------------------------------------------------------------
# 编译标准
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

option(BUILD_TESTING "构建单元测试" ON)

# ---------------------------------------------------------------------------
# 第三方依赖统一放在 third-party/
#   boost/asio : 仅头文件, 通过 BOOST_ROOT 定位 (体积过大, 不复制进仓库)
#   jsoncpp    : 已 vendored 到 third-party/jsoncpp (0.5.0)
#   spdlog     : 日志, FetchContent 下载到 third-party/_fetchcontent
#   googletest : 单元测试, FetchContent 下载到 third-party/_fetchcontent
# ---------------------------------------------------------------------------
set(FETCHCONTENT_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third-party/_fetchcontent")
include(FetchContent)

# --- boost/asio (header-only) ---
if(NOT BOOST_ROOT)
    if(DEFINED ENV{BOOST_ROOT})
        set(BOOST_ROOT "$ENV{BOOST_ROOT}")
    endif()
endif()
if(NOT BOOST_ROOT)
    foreach(candidate
            "D:/Program Files/boost"
            "C:/Program Files/boost"
            "C:/local/boost"
            "/usr/local"
            "/usr")
        if(EXISTS "${candidate}/boost/asio.hpp")
            set(BOOST_ROOT "${candidate}")
            break()
        endif()
    endforeach()
endif()
if(NOT EXISTS "${BOOST_ROOT}/boost/asio.hpp")
    message(FATAL_ERROR
        "未找到 Boost 头文件 (boost/asio.hpp)。\n"
        "请通过 -DBOOST_ROOT=<boost 安装根目录> 指定。")
endif()
message(STATUS "使用 Boost: ${BOOST_ROOT}")

add_library(boost_asio INTERFACE
        auth/src/main.cpp)
target_include_directories(boost_asio INTERFACE "${BOOST_ROOT}")
target_compile_definitions(boost_asio INTERFACE BOOST_ASIO_NO_DEPRECATED)
if(WIN32)
    # asio 在 Windows 下依赖 Winsock2
    target_compile_definitions(boost_asio INTERFACE _WIN32_WINNT=0x0601)
    target_link_libraries(boost_asio INTERFACE ws2_32 mswsock)
endif()

# --- jsoncpp (vendored, 0.5.0) ---
add_library(jsoncpp STATIC
    third-party/jsoncpp/src/lib_json/json_reader.cpp
    third-party/jsoncpp/src/lib_json/json_value.cpp
    third-party/jsoncpp/src/lib_json/json_writer.cpp)
target_include_directories(jsoncpp PUBLIC third-party/jsoncpp/include)
if(NOT MSVC)
    # 旧库 (2013 年) 在新编译器下告警较多, 屏蔽告警
    target_compile_options(jsoncpp PRIVATE -w)
endif()

# --- spdlog (日志) ---
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.15.3)
FetchContent_MakeAvailable(spdlog)

# --- googletest (单元测试) ---
if(BUILD_TESTING)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2)
    FetchContent_MakeAvailable(googletest)
    enable_testing()
endif()

# ---------------------------------------------------------------------------
# 子模块 (M0 仅启用 common + gateway, 其余后续里程碑逐步接入)
# ---------------------------------------------------------------------------
add_subdirectory(common)
add_subdirectory(gateway)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# 预留目录, 后续里程碑启用:
# add_subdirectory(proto)
# add_subdirectory(logic)
# add_subdirectory(registry)
# add_subdirectory(auth)
# add_subdirectory(resource)
