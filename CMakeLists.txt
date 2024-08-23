
cmake_minimum_required(VERSION 3.15)
project(ST)
find_package(protobuf REQUIRED)
find_package(gRPC REQUIRED)
find_package(absl REQUIRED)

file(GLOB_RECURSE server_srcs CONFIGURE_DEPENDS
    "example/chat_server.cc"
)

file(GLOB_RECURSE client_srcs CONFIGURE_DEPENDS
    "example/chat_client.cc"
)


add_executable(chat_client ${client_srcs})
add_executable(chat_server ${server_srcs})

target_link_libraries(chat_client protobuf::protobuf)
target_link_libraries(chat_client grpc::grpc)
target_include_directories(chat_client PRIVATE include)

target_link_libraries(chat_server protobuf::protobuf)
target_link_libraries(chat_server grpc::grpc)
target_link_libraries(chat_server abseil::abseil)
target_include_directories(chat_server PRIVATE include)

add_subdirectory(proto)
target_include_directories(chat_client PUBLIC ${CMAKE_BINARY_DIR}/proto)
target_link_libraries(chat_client grpc_proto)

target_include_directories(chat_server PUBLIC ${CMAKE_BINARY_DIR}/proto)
target_link_libraries(chat_server grpc_proto)

cmake_minimum_required(VERSION 3.15)
project(ST)
find_package(protobuf REQUIRED)
find_package(gRPC REQUIRED)