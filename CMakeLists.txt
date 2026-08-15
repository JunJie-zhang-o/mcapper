cmake_minimum_required(VERSION 3.16)

project(mcapper LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(FLIGHTLOGGER_BUILD_EXAMPLES "Build flight logger examples" ON)
option(FLIGHTLOGGER_ENABLE_MSGPACK "Enable reflect-cpp MsgPack support" OFF)
option(FLIGHTLOGGER_STATIC_RUNTIME "Statically link the C++ and GCC runtimes into executables" ON)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 12)
    message(FATAL_ERROR
        "mcapper requires GCC 12 or newer for C++20. "
        "Install gcc-12/g++-12 and configure with that compiler.")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16)
    message(FATAL_ERROR
        "mcapper requires Clang 16 or newer for C++20.")
endif()


find_package(Threads REQUIRED)


set(REFLECT_CPP_CORE_SOURCE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/reflect-cpp/src/reflectcpp.cpp)
set(REFLECT_CPP_JSON_SOURCE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/reflect-cpp/src/reflectcpp_json.cpp)
set(REFLECT_CPP_YYJSON_SOURCE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/reflect-cpp/src/yyjson.c)

add_library(flight_logger STATIC
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

if(FLIGHTLOGGER_BUILD_EXAMPLES)
    add_executable(flight_logger_smoke
        examples/flight_recorder_smoke.cpp)

    target_link_libraries(flight_logger_smoke
        PRIVATE
        flight_logger_rfl)
endif()

install(TARGETS flight_logger flight_logger_rfl_deps
    ARCHIVE DESTINATION lib)

if(TARGET flight_logger_smoke)
    install(TARGETS flight_logger_smoke
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
