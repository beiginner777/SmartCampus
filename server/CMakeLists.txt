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

# 强制静态链接（hiredis 会把 BUILD_SHARED_LIBS 翻成 ON，导致运行期缺 DLL）
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# ---------------------------------------------------------------------------
# 第三方依赖统一放在 third-party/
#   boost/asio : 仅头文件, 通过 BOOST_ROOT 定位 (体积过大, 不复制进仓库)
#   jsoncpp    : 已 vendored 到 third-party/jsoncpp (0.5.0)
#   spdlog     : 日志, FetchContent 下载到 third-party/_fetchcontent
#   googletest : 单元测试, FetchContent 下载到 third-party/_fetchcontent
# ---------------------------------------------------------------------------
set(FETCHCONTENT_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third-party/_fetchcontent")
include(FetchContent)

# 若 third-party/_fetchcontent 下已有解压好的源码，则直接复用（避免联网下载、加快 configure）；
# 否则走 URL + URL_HASH 下载（首次下载后缓存，后续 configure 只校验 hash、不会重复下载）。
foreach(_dep IN ITEMS spdlog googletest hiredis)
    string(TOUPPER ${_dep} _dep_upper)
    set(_dep_src "${CMAKE_CURRENT_SOURCE_DIR}/third-party/_fetchcontent/${_dep}-src")
    if(EXISTS "${_dep_src}" AND NOT DEFINED FETCHCONTENT_SOURCE_DIR_${_dep_upper})
        set(FETCHCONTENT_SOURCE_DIR_${_dep_upper} "${_dep_src}" CACHE PATH "" FORCE)
        message(STATUS "使用本地源码 ${_dep}: ${_dep_src}")
    endif()
endforeach()

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

add_library(boost_asio INTERFACE)
target_include_directories(boost_asio INTERFACE "${BOOST_ROOT}")
target_compile_definitions(boost_asio INTERFACE BOOST_ASIO_NO_DEPRECATED)
if(WIN32)
    # asio 在 Windows 下依赖 Winsock2
    target_compile_definitions(boost_asio INTERFACE _WIN32_WINNT=0x0601)
    target_link_libraries(boost_asio INTERFACE ws2_32 mswsock)
endif()

# --- libpq (PostgreSQL 官方 C 客户端) ---
if(NOT POSTGRES_ROOT)
    file(GLOB _pg_candidates "D:/Program Files/PostgreSQL/*")
    foreach(_pg ${_pg_candidates})
        if(EXISTS "${_pg}/include/libpq-fe.h")
            set(POSTGRES_ROOT "${_pg}")
            break()
        endif()
    endforeach()
endif()
if(NOT EXISTS "${POSTGRES_ROOT}/include/libpq-fe.h")
    message(FATAL_ERROR
        "未找到 PostgreSQL 头文件 (libpq-fe.h)。\n"
        "请通过 -DPOSTGRES_ROOT=<PostgreSQL 安装目录> 指定。")
endif()
message(STATUS "使用 PostgreSQL: ${POSTGRES_ROOT}")

add_library(postgres_libpq INTERFACE)
target_include_directories(postgres_libpq INTERFACE "${POSTGRES_ROOT}/include")
if(MSVC)
    # MSVC 直接链 PG 自带的导入库
    target_link_libraries(postgres_libpq INTERFACE "${POSTGRES_ROOT}/lib/libpq.lib")
else()
    # MinGW 用 gendef+dlltool 从 libpq.dll 生成的导入库（见 third-party/libpq/README.md）
    set(_libpq_implib "${CMAKE_SOURCE_DIR}/third-party/libpq/libpq.dll.a")
    if(NOT EXISTS "${_libpq_implib}")
        message(FATAL_ERROR
            "缺少 libpq 导入库 ${_libpq_implib}。\n"
            "请按 third-party/libpq/README.md 用 gendef+dlltool 从 libpq.dll 生成。")
    endif()
    target_link_libraries(postgres_libpq INTERFACE "${_libpq_implib}")
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
    URL https://github.com/gabime/spdlog/archive/refs/tags/v1.15.3.tar.gz
    URL_HASH SHA256=15a04e69c222eb6c01094b5c7ff8a249b36bb22788d72519646fb85feb267e67)
FetchContent_MakeAvailable(spdlog)

# --- googletest (单元测试) ---
if(BUILD_TESTING)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
        URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926)
    FetchContent_MakeAvailable(googletest)
    enable_testing()
endif()

# --- hiredis (Redis 官方 C 客户端) ---
set(DISABLE_TESTS ON CACHE BOOL "" FORCE)
set(ENABLE_SSL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    hiredis
    URL https://github.com/redis/hiredis/archive/refs/tags/v1.5.0.tar.gz
    URL_HASH SHA256=eec7ef8641d221e144a58d5ef8d733c8c75c7fd487097a233aacb4fa33981cfb)
FetchContent_MakeAvailable(hiredis)

# --- gRPC（复用 IM 项目已构建的 MSVC Debug 库，见 E:/File/jerry/IM/server/ChatServer1/grpc_json_protoc.props）---
if(MSVC)
    set(GRPC_ROOT "D:/Program Files/grpc/grpc")
    set(_grpc_vis "${GRPC_ROOT}/visualpro")

    set(_grpc_include_dirs
        "${GRPC_ROOT}/include"
        "${GRPC_ROOT}/third_party/protobuf/src"
        "${GRPC_ROOT}/third_party/abseil-cpp"
        "${GRPC_ROOT}/third_party/re2"
        "${GRPC_ROOT}/third_party/address_sorting/include")

    set(_grpc_lib_dirs
        "${_grpc_vis}/Debug"
        "${_grpc_vis}/third_party/abseil-cpp/absl/base/Debug"
        "${_grpc_vis}/third_party/abseil-cpp/absl/types/Debug"
        "${_grpc_vis}/third_party/abseil-cpp/absl/synchronization/Debug"
        "${_grpc_vis}/third_party/abseil-cpp/absl/status/Debug"
        "${_grpc_vis}/third_party/abseil-cpp/absl/random/Debug"
        "${_grpc_vis}/third_party/abseil-cpp/absl/flags/Debug"
        "${_grpc_vis}/third_party/abseil-cpp/absl/debugging/Debug"
        "${_grpc_vis}/third_party/abseil-cpp/absl/container/Debug"
        "${_grpc_vis}/third_party/abseil-cpp/absl/hash/Debug"
        "${_grpc_vis}/third_party/abseil-cpp/absl/numeric/Debug"
        "${_grpc_vis}/third_party/abseil-cpp/absl/time/Debug"
        "${_grpc_vis}/third_party/abseil-cpp/absl/strings/Debug"
        "${_grpc_vis}/third_party/boringssl-with-bazel/Debug"
        "${_grpc_vis}/third_party/protobuf/Debug"
        "${_grpc_vis}/third_party/zlib/Debug"
        "${_grpc_vis}/third_party/cares/cares/lib/Debug"
        "${_grpc_vis}/third_party/re2/Debug")

    add_library(grpc_deps INTERFACE)
    target_include_directories(grpc_deps INTERFACE ${_grpc_include_dirs})
    target_link_directories(grpc_deps INTERFACE ${_grpc_lib_dirs})
    target_link_libraries(grpc_deps INTERFACE
        libprotobufd.lib
        gpr.lib
        grpc.lib
        grpc++.lib
        grpc++_reflection.lib
        address_sorting.lib
        ws2_32.lib
        cares.lib
        zlibstaticd.lib
        upb.lib
        ssl.lib
        crypto.lib
        absl_bad_any_cast_impl.lib
        absl_bad_optional_access.lib
        absl_bad_variant_access.lib
        absl_base.lib
        absl_city.lib
        absl_civil_time.lib
        absl_cord.lib
        absl_debugging_internal.lib
        absl_demangle_internal.lib
        absl_examine_stack.lib
        absl_exponential_biased.lib
        absl_failure_signal_handler.lib
        absl_flags.lib
        absl_flags_config.lib
        absl_flags_internal.lib
        absl_flags_marshalling.lib
        absl_flags_parse.lib
        absl_flags_program_name.lib
        absl_flags_usage.lib
        absl_flags_usage_internal.lib
        absl_graphcycles_internal.lib
        absl_hash.lib
        absl_hashtablez_sampler.lib
        absl_int128.lib
        absl_leak_check.lib
        absl_leak_check_disable.lib
        absl_log_severity.lib
        absl_malloc_internal.lib
        absl_periodic_sampler.lib
        absl_random_distributions.lib
        absl_random_internal_distribution_test_util.lib
        absl_random_internal_pool_urbg.lib
        absl_random_internal_randen.lib
        absl_random_internal_randen_hwaes.lib
        absl_random_internal_randen_hwaes_impl.lib
        absl_random_internal_randen_slow.lib
        absl_random_internal_seed_material.lib
        absl_random_seed_gen_exception.lib
        absl_random_seed_sequences.lib
        absl_raw_hash_set.lib
        absl_raw_logging_internal.lib
        absl_scoped_set_env.lib
        absl_spinlock_wait.lib
        absl_stacktrace.lib
        absl_status.lib
        absl_strings.lib
        absl_strings_internal.lib
        absl_str_format_internal.lib
        absl_symbolize.lib
        absl_synchronization.lib
        absl_throw_delegate.lib
        absl_time.lib
        absl_time_zone.lib
        absl_statusor.lib
        re2.lib)
endif()

# ---------------------------------------------------------------------------
# 子模块 (M0 仅启用 common + gateway, 其余后续里程碑逐步接入)
# ---------------------------------------------------------------------------
add_subdirectory(common)
add_subdirectory(proto)
add_subdirectory(gateway)
add_subdirectory(auth)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# 预留目录, 后续里程碑启用:
# add_subdirectory(logic)
# add_subdirectory(registry)
# add_subdirectory(resource)
