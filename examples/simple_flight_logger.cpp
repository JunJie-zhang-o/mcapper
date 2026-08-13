#include <cstdint>
#include <iostream>
#include <vector>

#include <mcapper/flight_logger.hpp>

int main() {
  mcapper::LoggerOptions options;
  options.output_directory = "logs";
  options.base_filename = "example_flight";
  options.pre_trigger_duration_ns = 2'000'000'000ULL;
  options.post_trigger_duration_ns = 1'000'000'000ULL;
  options.default_topic = "/flight/example";

  mcapper::FlightLogger logger(options);

  for (std::uint64_t i = 0; i < 5; ++i) {
    logger.push(mcapper::Record{
        i * 1'000'000'000ULL,
        i * 1'000'000'000ULL,
        {},
        {},
        {},
        {},
        {},
        {static_cast<std::uint8_t>(i)},
    });
  }

  logger.trigger(4'000'000'000ULL);
  logger.push(mcapper::Record{4'500'000'000ULL, 4'500'000'000ULL, {}, {}, {}, {}, {}, {42}});
  logger.flush();

  std::cout << logger.last_output_path() << '\n';
  return 0;
}
