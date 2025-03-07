cmake_minimum_required(VERSION 3.8)

project(MyKVRaft C CXX)

# Copied from examples/cpp/cmake/common.cmake
# This branch assumes that gRPC and all its dependencies are already installed
# on this system, so they can be located by find_package().

# Find Protobuf installation
# Looks for protobuf-config.cmake file installed by Protobuf's cmake installation.
option(protobuf_MODULE_COMPATIBLE TRUE)
set(CMAKE_VERBOSE_MAKEFILE ON)

# 设置项目库文件输出的路径
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

# Guide CMAKE to where they can find protobuf-config.cmake & gRPCConfig.cmake
SET(ENV{CMAKE_PREFIX_PATH} "/root/.local/lib/cmake/:$ENV{CMAKE_PREFIX_PATH}")
find_package(Protobuf CONFIG REQUIRED)
message(STATUS "Using protobuf ${Protobuf_VERSION}")

set(_PROTOBUF_LIBPROTOBUF protobuf::libprotobuf)
set(_REFLECTION gRPC::grpc++_reflection)
if(CMAKE_CROSSCOMPILING)
  find_program(_PROTOBUF_PROTOC protoc)
else()
  set(_PROTOBUF_PROTOC $<TARGET_FILE:protobuf::protoc>)
endif()

# Find gRPC installation
# Looks for gRPCConfig.cmake file installed by gRPC's cmake installation.
find_package(gRPC CONFIG REQUIRED)
message(STATUS "Using gRPC ${gRPC_VERSION}")

set(_GRPC_GRPCPP gRPC::grpc++)
if(CMAKE_CROSSCOMPILING)
  find_program(_GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)
else()
  set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:gRPC::grpc_cpp_plugin>)
endif()

find_package(absl CONFIG REQUIRED)


include_directories(${PROJECT_SOURCE_DIR}/src/utils)
include_directories(${PROJECT_SOURCE_DIR}/src/raft)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/src/proto)

add_subdirectory(src)

add_subdirectory(examples)


# add_library(raft-core ${src_raft} ${src_utils})
# target_link_libraries(raft-core
#     RaftRpcs_grpc_proto
#     absl::check
#     absl::flags
#     absl::flags_parse
#     absl::log
#     ${_REFLECTION}
#     ${_GRPC_GRPCPP}
#     ${_PROTOBUF_LIBPROTOBUF})
