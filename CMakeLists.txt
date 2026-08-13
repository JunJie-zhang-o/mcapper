cmake_minimum_required(VERSION 3.16)

project(mcapper
  VERSION 0.1.0
  DESCRIPTION "C++ shared library for flight logging with lock-free ring buffer and MCAP output"
  LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile_commands.json for clangd" FORCE)

option(MCAPPER_BUILD_TESTS "Build mcapper tests" ON)
option(MCAPPER_BUILD_EXAMPLES "Build mcapper examples" ON)
option(MCAPPER_BUILD_ROS1 "Build ROS 1 adapter" OFF)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(YAML_CPP REQUIRED yaml-cpp)

add_library(mcapper SHARED
  src/capper_config.cpp
  src/flight_logger.cpp
  src/mcap_writer.cpp
  src/record_capper.cpp
  src/trigger.cpp
  src/version.cpp)

add_library(mcapper::mcapper ALIAS mcapper)

target_compile_features(mcapper PUBLIC cxx_std_17)
target_include_directories(mcapper
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${YAML_CPP_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_directories(mcapper PRIVATE ${YAML_CPP_LIBRARY_DIRS})
target_link_libraries(mcapper PRIVATE ${YAML_CPP_LIBRARIES} Threads::Threads)

set_target_properties(mcapper PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  EXPORT_NAME mcapper)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(MCAPPER_BUILD_EXAMPLES)
  add_executable(simple_flight_logger examples/simple_flight_logger.cpp)
  target_link_libraries(simple_flight_logger PRIVATE mcapper::mcapper)
endif()

if(MCAPPER_BUILD_ROS1)
  pkg_check_modules(ROS1 REQUIRED roscpp topic_tools)

  add_library(mcapper_ros1 SHARED src/ros1_capper.cpp)
  add_library(mcapper::ros1 ALIAS mcapper_ros1)
  target_compile_features(mcapper_ros1 PUBLIC cxx_std_17)
  target_include_directories(mcapper_ros1
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:include>
      ${ROS1_INCLUDE_DIRS})
  target_link_directories(mcapper_ros1 PUBLIC ${ROS1_LIBRARY_DIRS})
  target_link_libraries(mcapper_ros1 PUBLIC mcapper::mcapper ${ROS1_LIBRARIES})

  add_executable(mcapper_ros1_cli src/ros1_capper_cli.cpp)
  target_link_libraries(mcapper_ros1_cli PRIVATE mcapper::ros1)
endif()

if(MCAPPER_BUILD_TESTS)
  enable_testing()

  add_executable(ring_buffer_test test/ring_buffer_test.cpp)
  target_link_libraries(ring_buffer_test PRIVATE mcapper::mcapper)
  add_test(NAME ring_buffer_test COMMAND ring_buffer_test)

  add_executable(trigger_window_test test/trigger_window_test.cpp)
  target_link_libraries(trigger_window_test PRIVATE mcapper::mcapper)
  add_test(NAME trigger_window_test COMMAND trigger_window_test)

  add_executable(mcap_writer_test test/mcap_writer_test.cpp)
  target_link_libraries(mcap_writer_test PRIVATE mcapper::mcapper)
  add_test(NAME mcap_writer_test COMMAND mcap_writer_test)

  add_executable(capper_config_test test/capper_config_test.cpp)
  target_link_libraries(capper_config_test PRIVATE mcapper::mcapper)
  add_test(NAME capper_config_test COMMAND capper_config_test)

  add_executable(record_capper_test test/record_capper_test.cpp)
  target_link_libraries(record_capper_test PRIVATE mcapper::mcapper)
  add_test(NAME record_capper_test COMMAND record_capper_test)
endif()

install(TARGETS mcapper
  EXPORT mcapperTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(DIRECTORY include/mcapper
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(FILES package.xml
  DESTINATION ${CMAKE_INSTALL_DATADIR}/mcapper)

install(EXPORT mcapperTargets
  NAMESPACE mcapper::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mcapper)

configure_package_config_file(
  cmake/mcapperConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/mcapperConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mcapper)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/mcapperConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/mcapperConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/mcapperConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mcapper)

add_custom_target(clangd
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_BINARY_DIR}/compile_commands.json
    ${CMAKE_SOURCE_DIR}/compile_commands.json
  COMMENT "Copy compile_commands.json to source root for clangd")
