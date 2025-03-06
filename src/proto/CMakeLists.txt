foreach(_target
    RaftRpcs helloworld)
    get_filename_component(${_target}_proto "/root/MyKVRaft/src/proto/${_target}.proto" ABSOLUTE)
    get_filename_component(${_target}_proto_path "${${_target}_proto}" PATH)
    # Generated sources
    set(${_target}_proto_srcs "${CMAKE_CURRENT_BINARY_DIR}/${_target}.pb.cc")
    set(${_target}_proto_hdrs "${CMAKE_CURRENT_BINARY_DIR}/${_target}.pb.h")
    set(${_target}_grpc_srcs "${CMAKE_CURRENT_BINARY_DIR}/${_target}.grpc.pb.cc")
    set(${_target}_grpc_hdrs "${CMAKE_CURRENT_BINARY_DIR}/${_target}.grpc.pb.h")
    add_custom_command(
        OUTPUT "${${_target}_proto_srcs}" "${${_target}_proto_hdrs}" "${${_target}_grpc_srcs}" "${${_target}_grpc_hdrs}"
        COMMAND ${_PROTOBUF_PROTOC}
        ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
            --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
            -I "${${_target}_proto_path}"
            --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
            "${${_target}_proto}"
        DEPENDS "${${_target}_proto}")

    # Include generated *.pb.h files
    include_directories("${CMAKE_CURRENT_BINARY_DIR}")

    # rg_grpc_proto
    add_library(${_target}_grpc_proto
    ${${_target}_grpc_srcs}
    ${${_target}_grpc_hdrs}
    ${${_target}_proto_srcs}
    ${${_target}_proto_hdrs})
    target_link_libraries(${_target}_grpc_proto
    absl::absl_log
    ${_REFLECTION}
    ${_GRPC_GRPCPP}
    ${_PROTOBUF_LIBPROTOBUF})
endforeach()