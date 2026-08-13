cmake_minimum_required(VERSION 3.16)
project(shape_shifter_test)

# 使用 pkg-config 寻找 ROS 1 的相关库，不需要依赖 catkin 宏
find_package(PkgConfig REQUIRED)
pkg_check_modules(ROS REQUIRED roscpp topic_tools)

# 引入上层 mcapper 目录，以便链接 mcapper 库
set(MCAPPER_BUILD_ROS1 ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../ mcapper_build)

add_executable(shape_shifter_cli main.cpp)

# 包含头文件目录
target_include_directories(shape_shifter_cli PRIVATE ${ROS_INCLUDE_DIRS})

# 添加库文件目录（如果环境变量未默认包含）
target_link_directories(shape_shifter_cli PRIVATE ${ROS_LIBRARY_DIRS})

# 链接对应的 ROS 库以及 mcapper
target_link_libraries(shape_shifter_cli PRIVATE ${ROS_LIBRARIES} mcapper::ros1)
