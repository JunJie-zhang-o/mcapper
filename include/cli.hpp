#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "recorder.hpp"

namespace flightLogger
{

    struct RecorderCliOptions
    {
        std::string output{"./logs/ros1_dynamic"};
        std::size_t pre_capacity{4096};
        std::size_t post_capacity{4096};
        double      post_trigger_timeout_sec{1.0};

        void validate() const;
    };

    FlightRecorderOptions make_flight_recorder_options(const RecorderCliOptions& cli_options,
                                                       std::string_view source_kind);

}  // namespace flightLogger
