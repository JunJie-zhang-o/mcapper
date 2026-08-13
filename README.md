# mcapper

`mcapper` is a small C++ shared library for flight logging. It keeps a
pre-trigger window in a single-producer/single-consumer ring buffer and writes a
triggered recording window to MCAP.

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

For older CTest versions, run `ctest` from the `build` directory instead.

Common operations are also available through the top-level `Makefile`:

```sh
make build
make test
make clangd
make clean
```

## Config-Driven Capper

```yaml
logger:
  output_directory: logs
  base_filename: ros1_capture
  ring_buffer_capacity: 10000
  pre_trigger_duration_ns: 5000000000
  post_trigger_duration_ns: 0
source:
  type: ros1
  topics:
    - /camera/image_raw
    - /imu/data
```

The core library exposes `mcapper::Capper`, `mcapper::RecordCapper`, and
`mcapper::loadYamlConfig()`. ROS 1 support is built as an optional adapter:

```sh
cmake -S . -B build-ros1 -DMCAPPER_BUILD_ROS1=ON
cmake --build build-ros1 --target mcapper_ros1_cli
./build-ros1/mcapper_ros1_cli config.yaml
```

The ROS 1 adapter subscribes with `topic_tools::ShapeShifter`, records the
configured topic name, writes MCAP profile `ros1`, channel encoding `ros1`, and
schema encoding `ros1msg`, then asynchronously flushes the current trigger window
when `trigger()` is called.

## Minimal Example

```cpp
#include <mcapper/flight_logger.hpp>

mcapper::LoggerOptions options;
options.output_directory = "logs";
options.pre_trigger_duration_ns = 2'000'000'000ULL;
options.post_trigger_duration_ns = 1'000'000'000ULL;

mcapper::FlightLogger logger(options);
logger.push(mcapper::Record{1'000'000'000ULL, 1'000'000'000ULL, "/imu", "cdr", "Imu", "ros2msg", {}, {1, 2, 3}});
logger.trigger();
logger.push(mcapper::Record{1'500'000'000ULL, 1'500'000'000ULL, "/imu", "cdr", "Imu", "ros2msg", {}, {4, 5, 6}});
logger.flush();
```

## Packaging Notes

The package is pure CMake and installs:

- `libmcapper.so`
- public headers under `include/mcapper`
- CMake package files under `lib/cmake/mcapper`
- `package.xml` under `share/mcapper`

This layout is intended to be easy to wrap with `debian/` packaging later.
