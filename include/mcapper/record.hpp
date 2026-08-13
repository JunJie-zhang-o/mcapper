#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mcapper
{

    struct Record
    {
        std::uint64_t             log_time_ns{0};
        std::uint64_t             publish_time_ns{0};
        std::string               topic;
        std::string               message_encoding;
        std::string               schema_name;
        std::string               schema_encoding;
        std::vector<std::uint8_t> schema_data;
        std::vector<std::uint8_t> data;
    };

}  // namespace mcapper
