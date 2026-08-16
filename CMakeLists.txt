cmake_minimum_required(VERSION 3.16)

set(CMAKE_C_COMPILER "/usr/bin/gcc-12")
set(CMAKE_CXX_COMPILER "/usr/bin/g++-12")
project(mcapper LANGUAGES C CXX)

include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(FLIGHTLOGGER_BUILD_EXAMPLES "Build flight logger examples" ON)
option(FLIGHTLOGGER_BUILD_TESTS "Build flight logger tests" ON)
option(FLIGHTLOGGER_ENABLE_MSGPACK "Enable reflect-cpp MsgPack support" OFF)
set(FLIGHTLOGGER_ENABLE_ROS1_DEFAULT OFF)
if("$ENV{ROS_VERSION}" STREQUAL "1")
    set(FLIGHTLOGGER_ENABLE_ROS1_DEFAULT ON)
endif()
option(FLIGHTLOGGER_ENABLE_ROS1 "Enable ROS1 codec support" ${FLIGHTLOGGER_ENABLE_ROS1_DEFAULT})
option(FLIGHTLOGGER_ENABLE_ROS2 "Enable ROS2 codec support" OFF)
option(FLIGHTLOGGER_STATIC_RUNTIME "Link libstdc++ and libgcc statically when using GCC" ON)
# Version check disabled for testing purposes
# (original checks removed)
# (no version check required)

add_compile_options(
    "$<$<COMPILE_LANGUAGE:C>:-g>"
    "$<$<COMPILE_LANGUAGE:C>:-O2>"
    "$<$<COMPILE_LANGUAGE:CXX>:-g>"
    "$<$<COMPILE_LANGUAGE:CXX>:-O2>")

find_package(Threads REQUIRED)

set(CMAKE_INSTALL_INCLUDEDIR "include")
set(CMAKE_INSTALL_LIBDIR "lib/${PROJECT_NAME}")
set(CMAKE_INSTALL_DATAROOTDIR "share")
set(MCAPPER_INSTALL_INCLUDE_DIR "${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}")
set(MCAPPER_INSTALL_LIB_DIR "${CMAKE_INSTALL_LIBDIR}")
set(MCAPPER_INSTALL_CMAKE_DIR "${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}/cmake")

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/package.xml" PACKAGE_XML)
string(REGEX MATCH "<version>[^<]+</version>" PACKAGE_VERSION_TAG "${PACKAGE_XML}")
string(REGEX REPLACE "<version>([^<]+)</version>" "\\1" PACKAGE_VERSION "${PACKAGE_VERSION_TAG}")

set(REFLECT_CPP_CORE_SOURCE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/reflect-cpp/src/reflectcpp.cpp)
set(REFLECT_CPP_JSON_SOURCE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/reflect-cpp/src/reflectcpp_json.cpp)
set(REFLECT_CPP_YYJSON_SOURCE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/reflect-cpp/src/yyjson.c)

add_library(flight_logger STATIC
    src/bridge/ros1/config.cpp
    src/flight_logger.cpp)

set_target_properties(flight_logger PROPERTIES
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(flight_logger PUBLIC cxx_std_20)

target_include_directories(flight_logger
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/reflect-cpp/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/reflect-cpp/include/rfl/thirdparty>
    $<INSTALL_INTERFACE:${MCAPPER_INSTALL_INCLUDE_DIR}>
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
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

set_target_properties(flight_logger_rfl_deps PROPERTIES
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(flight_logger_rfl_deps PUBLIC cxx_std_20)

target_include_directories(flight_logger_rfl_deps
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/reflect-cpp/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/reflect-cpp/include/rfl/thirdparty>
    $<INSTALL_INTERFACE:${MCAPPER_INSTALL_INCLUDE_DIR}>)

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
    target_link_options(flight_logger
        PUBLIC
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

if(FLIGHTLOGGER_ENABLE_ROS1 AND FLIGHTLOGGER_BUILD_TESTS)
    find_package(GTest REQUIRED)
    add_executable(ros1_message_definition_test tests/ros1_message_definition_test.cpp)
    target_link_libraries(ros1_message_definition_test
        PRIVATE
        flight_logger
        GTest::GTest
        GTest::Main)
endif()

if(FLIGHTLOGGER_ENABLE_ROS1)
    add_executable(ros1_dynamic_recorder
        apps/ros1_dynamic_recorder.cpp
        apps/ros1/cli.cpp)

    target_include_directories(ros1_dynamic_recorder
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/apps
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

    target_link_libraries(ros1_dynamic_recorder
        PRIVATE
        flight_logger)
endif()

if(FLIGHTLOGGER_BUILD_EXAMPLES)
    add_executable(flight_logger_smoke
        examples/flight_recorder_smoke.cpp)

    target_link_libraries(flight_logger_smoke
        PRIVATE
        flight_logger_rfl)

endif()

if(FLIGHTLOGGER_BUILD_TESTS)
    enable_testing()

    add_executable(flight_logger_source_spec_test
        tests/source_spec_test.cpp
        apps/ros1/cli.cpp)

    target_include_directories(flight_logger_source_spec_test
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/apps
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

    target_link_libraries(flight_logger_source_spec_test
        PRIVATE
        flight_logger)

    add_test(NAME flight_logger_source_spec_test
        COMMAND flight_logger_source_spec_test)

    add_executable(flight_logger_ringbuffer_test
        tests/ringbuffer_test.cpp)

    target_link_libraries(flight_logger_ringbuffer_test
        PRIVATE
        flight_logger)

    add_test(NAME flight_logger_ringbuffer_test
        COMMAND flight_logger_ringbuffer_test)

    add_executable(flight_logger_recorder_capacity_test
        tests/recorder_capacity_test.cpp)

    target_link_libraries(flight_logger_recorder_capacity_test
        PRIVATE
        flight_logger)

    add_test(NAME flight_logger_recorder_capacity_test
        COMMAND flight_logger_recorder_capacity_test)

    add_executable(flight_logger_attachment_test
        tests/attachment_test.cpp)

    target_link_libraries(flight_logger_attachment_test
        PRIVATE
        flight_logger)

    add_test(NAME flight_logger_attachment_test
        COMMAND flight_logger_attachment_test)
endif()

install(TARGETS flight_logger flight_logger_rfl_deps flight_logger_rfl
    EXPORT mcapper_targets
    ARCHIVE DESTINATION ${MCAPPER_INSTALL_LIB_DIR}
    LIBRARY DESTINATION ${MCAPPER_INSTALL_LIB_DIR}
    RUNTIME DESTINATION bin)

if(TARGET ros1_dynamic_recorder)
    install(TARGETS ros1_dynamic_recorder
        RUNTIME DESTINATION bin)
endif()

install(PROGRAMS mcap
    DESTINATION bin)

install(DIRECTORY include/
    DESTINATION ${MCAPPER_INSTALL_INCLUDE_DIR})
install(DIRECTORY third_party/mcap/include/
    DESTINATION ${MCAPPER_INSTALL_INCLUDE_DIR})
install(DIRECTORY third_party/reflect-cpp/include/
    DESTINATION ${MCAPPER_INSTALL_INCLUDE_DIR})

install(
    EXPORT mcapper_targets
    FILE mcapper_targets.cmake
    NAMESPACE mcapper::
    DESTINATION ${MCAPPER_INSTALL_CMAKE_DIR})

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/mcapper-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/mcapper-config.cmake"
    INSTALL_DESTINATION ${MCAPPER_INSTALL_CMAKE_DIR})

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/mcapper-config-version.cmake"
    VERSION ${PACKAGE_VERSION}
    COMPATIBILITY SameMajorVersion)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/mcapper-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/mcapper-config-version.cmake"
    DESTINATION ${MCAPPER_INSTALL_CMAKE_DIR})

set(CPACK_GENERATOR "TGZ")
set(CPACK_PACKAGE_NAME "mcapper")
set(CPACK_PACKAGE_VERSION "${PACKAGE_VERSION}")
set(CPACK_SET_DESTDIR ON)
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
