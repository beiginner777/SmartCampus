# 接入网关 (C++ Asio 引擎)
add_executable(gateway
    src/main.cpp
    src/tcp_server.cpp
    src/session.cpp
    src/auth_service.cpp)

target_include_directories(gateway PRIVATE include)

target_link_libraries(gateway PRIVATE
    common
    boost_asio)
