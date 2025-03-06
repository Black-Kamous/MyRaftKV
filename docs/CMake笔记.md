# CMake项目结构

1. 在项目顶层的CMakeLists.txt文件中，需要写cmake_minimum_required，project等项目信息，和一些全局的基本配置项，如编译器位置，find_package配置外部库等，可以include_directories列出全部的头文件位置
2. 在每层代码目录中配置一个CMakeLists.txt，上层通过add_subdirectory联系下层目录

# protobuf编译

参考grpc项目CMakeList，主要命令为

```C
add_custom_command(
        OUTPUT "${${_target}_proto_srcs}" "${${_target}_proto_hdrs}" "${${_target}_grpc_srcs}" "${${_target}_grpc_hdrs}"
        COMMAND ${_PROTOBUF_PROTOC}
        ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
            --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
            -I "${${_target}_proto_path}"
            --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
            "${${_target}_proto}"
        DEPENDS "${${_target}_proto}")
```

构造了合适的protoc命令，可以看到一个.proto文件对应了4个输出文件，两个.pb，两个.grpc.pb，接下来就可以将这四个文件编译成一个库文件

```C
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
```

这样做的结果是pb文件和静态库都在build及其子目录下（是更合理的做法，因为pb文件由proto文件生成，且不应手动修改，源文件中只保留proto即可）

其他源代码目录想要引用.pb.h则需要添加一行

```
include_directories(${CMAKE_CURRENT_BINARY_DIR}/../src/proto)
```

其中的CMAKE_CURRENT_BINARY_DIR代表了CMake生成目录中的位置（build目录下），通过这个变量可以在生成内容之间产生索引（实际上满足了手写源代码依赖被生成源代码的关系）
