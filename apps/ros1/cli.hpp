#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "bridge/ros1/config.hpp"
#include "recorder.hpp"

namespace CLI
{
    class App;
}

namespace flightLogger
{

    struct Ros1BridgeCliOptions
    {
        uint32_t                 spinner_threads{4};
        uint32_t                 queue_size{5};
        std::vector<std::string> sources;

        void validate() const;
    };

    struct Ros1DynamicRecorderCliOptions
    {
        FlightRecorderOptions        recorder_options;
        Ros1BridgeCliOptions         ros1_options;
        std::vector<Ros1TopicConfig> topics;
    };

    Ros1TopicConfig parse_ros1_topic_config(std::string_view input,
                                            std::size_t default_pre_capacity,
                                            std::size_t default_post_capacity);
    std::vector<Ros1TopicConfig> parse_ros1_topic_configs(const std::vector<std::string>& inputs,
                                                          const FlightRecorderOptions& recorder_options);

    Ros1DynamicRecorderCliOptions parse_ros1_dynamic_recorder_cli(CLI::App& app, int argc, char* argv[]);
    Ros1DynamicRecorderCliOptions parse_ros1_dynamic_recorder_cli(int argc, char* argv[]);

}  // namespace flightLogger
