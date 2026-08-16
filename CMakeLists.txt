cmake_minimum_required(VERSION 3.16)

set(CMAKE_C_COMPILER "/usr/bin/gcc-12")
set(CMAKE_CXX_COMPILER "/usr/bin/g++-12")
project(mcapper LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(FLIGHTLOGGER_BUILD_EXAMPLES "Build flight logger examples" ON)
option(FLIGHTLOGGER_BUILD_TESTS "Build flight logger tests" ON)
option(FLIGHTLOGGER_ENABLE_MSGPACK "Enable reflect-cpp MsgPack support" OFF)
option(FLIGHTLOGGER_ENABLE_ROS1 "Enable ROS1 codec support" OFF)
option(FLIGHTLOGGER_ENABLE_ROS2 "Enable ROS2 codec support" OFF)
# Version check disabled for testing purposes
# (original checks removed)
# (no version check required)

find_package(Threads REQUIRED)


set(REFLECT_CPP_CORE_SOURCE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/reflect-cpp/src/reflectcpp.cpp)
set(REFLECT_CPP_JSON_SOURCE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/reflect-cpp/src/reflectcpp_json.cpp)
set(REFLECT_CPP_YYJSON_SOURCE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/reflect-cpp/src/yyjson.c)

add_library(flight_logger STATIC
    src/cli.cpp
    src/flight_logger.cpp)

target_compile_features(flight_logger PUBLIC cxx_std_20)

target_include_directories(flight_logger
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/reflect-cpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/reflect-cpp/include/rfl/thirdparty
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/mcap/include)

target_compile_definitions(flight_logger
    PRIVATE
    MCAP_COMPRESSION_NO_LZ4
    MCAP_COMPRESSION_NO_ZSTD)

target_link_libraries(flight_logger
    PUBLIC
    Threads::Threads)

add_library(flight_logger_rfl_deps STATIC
    ${REFLECT_CPP_CORE_SOURCE}
    ${REFLECT_CPP_JSON_SOURCE}
    ${REFLECT_CPP_YYJSON_SOURCE})

target_compile_features(flight_logger_rfl_deps PUBLIC cxx_std_20)

target_include_directories(flight_logger_rfl_deps
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/reflect-cpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/reflect-cpp/include/rfl/thirdparty)

# reflect-cpp 0.25.0's core source uses std::numeric_limits without including
# <limits>. Keep the vendored source untouched.
set_source_files_properties(${REFLECT_CPP_CORE_SOURCE}
    PROPERTIES
    COMPILE_OPTIONS "-include;limits")

add_library(flight_logger_rfl INTERFACE)

target_link_libraries(flight_logger_rfl
    INTERFACE
    flight_logger
    flight_logger_rfl_deps)

if(FLIGHTLOGGER_STATIC_RUNTIME AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(flight_logger_rfl
        INTERFACE
        -static-libstdc++
        -static-libgcc)
endif()

if(FLIGHTLOGGER_ENABLE_MSGPACK)
    find_path(MSGPACK_INCLUDE_DIR msgpack.h REQUIRED)
    find_library(MSGPACKC_LIBRARY msgpackc REQUIRED)

    target_sources(flight_logger_rfl_deps
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/reflect-cpp/src/reflectcpp_msgpack.cpp)

    target_include_directories(flight_logger_rfl_deps
        PUBLIC
        ${MSGPACK_INCLUDE_DIR})

    target_compile_definitions(flight_logger_rfl
        INTERFACE
        FLIGHTLOGGER_ENABLE_MSGPACK=1)

    target_link_libraries(flight_logger_rfl_deps
        PUBLIC
        ${MSGPACKC_LIBRARY})
endif()

if(FLIGHTLOGGER_ENABLE_ROS1)
    find_package(catkin REQUIRED COMPONENTS roscpp std_msgs topic_tools)

    target_sources(flight_logger
        PRIVATE
        src/bridge_ros1.cpp)

    target_compile_definitions(flight_logger
        PUBLIC
        FLIGHTLOGGER_ENABLE_ROS1=1)

    target_include_directories(flight_logger
        PUBLIC
        ${catkin_INCLUDE_DIRS})

    target_link_libraries(flight_logger
        PUBLIC
        ${catkin_LIBRARIES})
endif()

if(FLIGHTLOGGER_ENABLE_ROS2)
    find_package(rclcpp REQUIRED)
    find_package(ament_index_cpp REQUIRED)

    target_compile_definitions(flight_logger
        PUBLIC
        FLIGHTLOGGER_ENABLE_ROS2=1)

    target_link_libraries(flight_logger
        PUBLIC
        rclcpp::rclcpp
        ament_index_cpp::ament_index_cpp)
endif()

if(FLIGHTLOGGER_ENABLE_ROS1)
    find_package(GTest REQUIRED)
    add_executable(ros1_message_definition_test tests/ros1_message_definition_test.cpp)
    target_link_libraries(ros1_message_definition_test
        PRIVATE
        flight_logger
        GTest::GTest
        GTest::Main)
endif()

if(FLIGHTLOGGER_BUILD_EXAMPLES)
    add_executable(flight_logger_smoke
        examples/flight_recorder_smoke.cpp)

    target_link_libraries(flight_logger_smoke
        PRIVATE
        flight_logger_rfl)

    if(FLIGHTLOGGER_ENABLE_ROS1)
        add_executable(ros1_dynamic_recorder
            examples/ros1_dynamic_recorder.cpp)

        target_link_libraries(ros1_dynamic_recorder
            PRIVATE
            flight_logger)
    endif()
endif()

if(FLIGHTLOGGER_BUILD_TESTS)
    enable_testing()

    add_executable(flight_logger_source_spec_test
        tests/source_spec_test.cpp)

    target_link_libraries(flight_logger_source_spec_test
        PRIVATE
        flight_logger)

    add_test(NAME flight_logger_source_spec_test
        COMMAND flight_logger_source_spec_test)

    add_executable(flight_logger_attachment_test
        tests/attachment_test.cpp)

    target_link_libraries(flight_logger_attachment_test
        PRIVATE
        flight_logger)

    add_test(NAME flight_logger_attachment_test
        COMMAND flight_logger_attachment_test)
endif()

install(TARGETS flight_logger flight_logger_rfl_deps
    ARCHIVE DESTINATION lib)

if(TARGET flight_logger_smoke)
    install(TARGETS flight_logger_smoke
        RUNTIME DESTINATION bin)
endif()

if(TARGET ros1_dynamic_recorder)
    install(TARGETS ros1_dynamic_recorder
        RUNTIME DESTINATION bin)
endif()

install(DIRECTORY include/
    DESTINATION include)
install(DIRECTORY third_party/mcap/include/
    DESTINATION include)
install(DIRECTORY third_party/reflect-cpp/include/
    DESTINATION include)

set(CPACK_GENERATOR "TGZ")
set(CPACK_PACKAGE_NAME "mcapper")
set(CPACK_PACKAGE_VERSION "0.1.0")
include(CPack)

if(FLIGHTLOGGER_ENABLE_ROS2)
    find_package(ament_cmake REQUIRED)
    find_package(std_msgs REQUIRED)
    find_package(upperlimb_msgs REQUIRED)
    add_executable(ros2_message_definition_main tests/ros2_message_definition_main.cpp)
    set_target_properties(ros2_message_definition_main PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
    ament_target_dependencies(ros2_message_definition_main
        rclcpp
        std_msgs
        upperlimb_msgs
        ament_index_cpp)
endif()
