#include <cassert>
#include <filesystem>

#include <mcapper/flight_logger.hpp>

int main() {
  mcapper::LoggerOptions options;
  options.output_directory = "test_logs";
  options.base_filename = "trigger_window";
  options.pre_trigger_duration_ns = 2'000'000'000ULL;
  options.post_trigger_duration_ns = 1'000'000'000ULL;
  options.ring_buffer_capacity = 8;

  mcapper::FlightLogger logger(options);
  logger.push(mcapper::Record{1'000'000'000ULL, 1'000'000'000ULL, {}, {}, {}, {}, {}, {1}});
  logger.push(mcapper::Record{2'000'000'000ULL, 2'000'000'000ULL, {}, {}, {}, {}, {}, {2}});
  logger.push(mcapper::Record{3'000'000'000ULL, 3'000'000'000ULL, {}, {}, {}, {}, {}, {3}});

  assert(logger.trigger(3'000'000'000ULL));
  assert(logger.state() == mcapper::Trigger::State::RecordingPostTrigger);

  assert(logger.push(mcapper::Record{3'500'000'000ULL, 3'500'000'000ULL, {}, {}, {}, {}, {}, {4}}));
  assert(logger.push(mcapper::Record{4'500'000'000ULL, 4'500'000'000ULL, {}, {}, {}, {}, {}, {5}}));
  assert(logger.state() == mcapper::Trigger::State::Complete);
  assert(!logger.last_output_path().empty());
  assert(std::filesystem::exists(logger.last_output_path()));

  return 0;
}
