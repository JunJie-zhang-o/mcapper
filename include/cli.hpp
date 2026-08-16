#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "recorder.hpp"

namespace flightLogger
{

    struct SourceSpec
    {
        std::string scheme;
        std::string target;
    };

    struct DynamicRecorderCliOptions
    {
        bool                          help{false};
        FlightRecorderOptions         recorder_options;
        std::vector<SourceSpec>       sources;
    };

    SourceSpec parse_source_spec(std::string_view input);
    std::vector<SourceSpec> parse_source_specs(std::span<const std::string> inputs);

    DynamicRecorderCliOptions parse_dynamic_recorder_cli(int argc, char* argv[]);
    std::string dynamic_recorder_usage(std::string_view program);

}  // namespace flightLogger
