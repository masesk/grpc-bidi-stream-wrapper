find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

file(GLOB_RECURSE proto_srcs CONFIGURE_DEPENDS
    "*.proto"
)
add_library(grpc_proto ${proto_srcs})
target_link_libraries(grpc_proto 
    PUBLIC 
    grpc::grpc
    protobuf::protobuf
    gRPC::grpc++
)
target_include_directories(grpc_proto
  PUBLIC
  .
  ${CMAKE_CURRENT_BINARY_DIR}
  ${Protobuf_INCLUDE_DIRS}
)
get_target_property(grpc_cpp_plugin_location gRPC::grpc_cpp_plugin LOCATION)

protobuf_generate( TARGET grpc_proto LANGUAGE CPP PROTOS ${proto_srcs} )
protobuf_generate( 
    TARGET
        grpc_proto 
    LANGUAGE
        grpc 
    PROTOS 
        ${proto_srcs}
    GENERATE_EXTENSIONS
        .grpc.pb.h
        .grpc.pb.cc
    PLUGIN
        "protoc-gen-grpc=${grpc_cpp_plugin_location}"
)