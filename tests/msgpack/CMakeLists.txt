cmake_minimum_required(VERSION 3.16)
project(msgpack_mcap_test)

# 使用 pkg-config 寻找系统的 msgpack 库
find_package(PkgConfig REQUIRED)
pkg_check_modules(MSGPACK REQUIRED msgpack)

# 引入上层 mcapper 目录，以便链接 mcapper 库
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../ mcapper_build)

add_executable(msgpack_mcap_cli main.cpp)

target_include_directories(msgpack_mcap_cli PRIVATE ${MSGPACK_INCLUDE_DIRS})
target_link_directories(msgpack_mcap_cli PRIVATE ${MSGPACK_LIBRARY_DIRS})

# 链接 msgpack 和 mcapper
target_link_libraries(msgpack_mcap_cli PRIVATE ${MSGPACK_LIBRARIES} mcapper::mcapper)
