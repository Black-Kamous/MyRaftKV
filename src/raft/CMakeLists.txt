# 获取当前源文件所在目录的绝对路径
get_filename_component(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}" ABSOLUTE)

# 将该目录下所有源文件添加到 SRC_LIST 变量中
set(SRC_LIST
    ${PROJECT_SOURCE_DIR}/src/raft/raft.cc
)

set(src_raft ${SRC_LIST} CACHE INTERNAL "Description of the variable")