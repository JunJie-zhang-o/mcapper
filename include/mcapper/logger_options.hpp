#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mcapper
{

    struct LoggerOptions
    {
        std::uint64_t             pre_trigger_duration_ns{5'000'000'000ULL};
        std::uint64_t             post_trigger_duration_ns{5'000'000'000ULL};
        std::size_t               ring_buffer_capacity{4096};
        std::string               output_directory{"."};
        std::string               base_filename{"flight_log"};
        std::string               profile{"flightlog"};
        std::string               library{"mcapper"};
        std::string               default_topic{"/flight"};
        std::string               default_message_encoding{"raw"};
        std::string               default_schema_name{"mcapper.Record"};
        std::string               default_schema_encoding{"raw"};
        std::vector<std::uint8_t> default_schema_data{};
    };

}  // namespace mcapper
