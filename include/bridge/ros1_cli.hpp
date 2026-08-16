#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cli.hpp"
#include "recorder.hpp"

namespace flightLogger
{

    struct Ros1TopicConfig
    {
        std::string topic;
        std::size_t pre_capacity{4096};
        std::size_t post_capacity{4096};

        void validate() const;
    };

    struct Ros1BridgeCliOptions
    {
        uint32_t                 spinner_threads{4};
        uint32_t                 queue_size{5};
        std::vector<std::string> sources;

        void validate() const;
    };

    struct Ros1DynamicRecorderCliOptions
    {
        bool                         help{false};
        FlightRecorderOptions        recorder_options;
        Ros1BridgeCliOptions         ros1_options;
        std::vector<Ros1TopicConfig> topics;
    };

    Ros1TopicConfig parse_ros1_topic_config(std::string_view input,
                                            std::size_t default_pre_capacity,
                                            std::size_t default_post_capacity);
    std::vector<Ros1TopicConfig> parse_ros1_topic_configs(std::span<const std::string> inputs,
                                                          const RecorderCliOptions& recorder_options);

    Ros1DynamicRecorderCliOptions parse_ros1_dynamic_recorder_cli(int argc, char* argv[]);
    std::string ros1_dynamic_recorder_usage(std::string_view program);

}  // namespace flightLogger
