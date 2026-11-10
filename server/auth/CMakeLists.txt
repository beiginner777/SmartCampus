# 认证微服务（beast HTTP）：注册 / 登录 / 刷新，签发 JWT
add_executable(auth
    src/main.cpp
    src/http_server.cpp
    src/auth_handler.cpp)

target_include_directories(auth PRIVATE include)

target_link_libraries(auth PRIVATE
    common
    boost_asio
    gateway_proto)
