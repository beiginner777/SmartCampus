# 网关控制面 gRPC 接口（gateway.proto 由 protoc + grpc_cpp_plugin 生成）
# 生成命令见 gateway.proto 顶部注释 / 需要时手动重跑：
#   protoc -I proto --cpp_out=proto --grpc_out=proto \
#     --plugin=protoc-gen-grpc=<grpc_cpp_plugin.exe> proto/gateway.proto
add_library(gateway_proto STATIC
    gateway.pb.cc
    gateway.grpc.pb.cc)

target_include_directories(gateway_proto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(gateway_proto PUBLIC
    grpc_deps)
