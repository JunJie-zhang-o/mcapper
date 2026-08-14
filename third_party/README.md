# Third-party dependencies

This directory vendors small header-only dependencies used by this project.
Vendored source files are copied from upstream without local modifications.

## tcb span

- Source: https://github.com/tcbrindle/span
- Version: master commit `836dc6a0efd9849cb194e88e4aa2387436bb079b`
- License: BSL-1.0, see `tcb/LICENSE_1_0.txt`
- Include path: `third_party/tcb/include`
- Usage: `#include <tcb/span.hpp>`

## MCAP C++ API

- Source: https://github.com/foxglove/mcap
- Version: `releases/cpp/v2.1.3`, commit `1420296ffcfdcde4b6894c0c1aba0ad083f93dde`
- License: MIT, see `mcap/LICENSE`
- Include path: `third_party/mcap/include`
- Usage: include official headers such as `#include <mcap/writer.hpp>` or
  `#include <mcap/reader.hpp>`

MCAP's C++ implementation is header-only. Define `MCAP_IMPLEMENTATION` in
exactly one `.cpp` file. By default, define `MCAP_COMPRESSION_NO_LZ4` and
`MCAP_COMPRESSION_NO_ZSTD` before including MCAP if the project does not link
against lz4 or zstd.

## reflect-cpp

- Source: https://github.com/getml/reflect-cpp
- Version: `0.25.0`, commit `b132dccce37a1ff71d3c24dedbc40a512b39d5ea`
- License: MIT, see `reflect-cpp/LICENSE`
- Include path: `third_party/reflect-cpp/include`
- Usage: include official headers such as `#include <rfl.hpp>` or
  `#include <rfl/json.hpp>`

reflect-cpp requires C++20 and is not header-only. For the base library, compile
and link `reflect-cpp/src/reflectcpp.cpp`. For the bundled JSON support, also
compile and link `reflect-cpp/src/reflectcpp_json.cpp` and
`reflect-cpp/src/yyjson.c`, and add `third_party/reflect-cpp/include/rfl/thirdparty`
to the include path. Other serialization formats require their corresponding
optional source files and external dependencies.
