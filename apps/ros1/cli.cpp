#include "ros1/cli.hpp"

#include "CLI11.hpp"

#include <charconv>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace flightLogger
{
    namespace
    {
        std::size_t parse_size(std::string_view input, std::string_view context)
        {
            std::size_t value = 0;
            const auto [end, error] = std::from_chars(input.data(), input.data() + input.size(), value);
            if (error != std::errc{} || end != input.data() + input.size())
            {
                throw std::invalid_argument("invalid integer value for " + std::string{context} + ": " + std::string{input});
            }

            return value;
        }

        std::vector<std::string> split_sources(std::string_view input)
        {
            std::vector<std::string> sources;
            std::size_t              start = 0;

            for (;;)
            {
                const auto separator = input.find(',', start);
                const auto end       = separator == std::string_view::npos ? input.size() : separator;
                sources.emplace_back(input.substr(start, end - start));

                if (separator == std::string_view::npos) break;
                start = separator + 1;
            }

            return sources;
        }
    }  // namespace

    void Ros1BridgeCliOptions::validate() const
    {
        if (spinner_threads == 0)
        {
            throw std::invalid_argument("invalid positive integer value for --ros1.spinner-threads");
        }
        if (queue_size == 0)
        {
            throw std::invalid_argument("invalid positive integer value for --ros1.queue-size");
        }
        if (sources.empty())
        {
            throw std::invalid_argument("--ros1.sources is required");
        }
    }

    Ros1TopicConfig parse_ros1_topic_config(std::string_view input,
                                            std::size_t default_pre_capacity,
                                            std::size_t default_post_capacity)
    {
        const auto scheme_separator = input.find(':');
        if (scheme_separator == std::string_view::npos || scheme_separator == 0)
        {
            throw std::invalid_argument("source must include an explicit scheme prefix, for example ros1:/imu/data");
        }

        const auto scheme = input.substr(0, scheme_separator);
        if (scheme != "ros1")
        {
            throw std::invalid_argument("unsupported source scheme: " + std::string{scheme});
        }

        auto topic = input.substr(scheme_separator + 1);
        std::size_t pre_capacity  = default_pre_capacity;
        std::size_t post_capacity = default_post_capacity;

        const auto last_separator = input.rfind(':');
        if (last_separator != scheme_separator)
        {
            const auto pre_separator = input.rfind(':', last_separator - 1);
            if (pre_separator == scheme_separator || pre_separator == std::string_view::npos)
            {
                throw std::invalid_argument("source capacity override must look like ros1:/topic:PRE:POST: " + std::string{input});
            }

            topic = input.substr(scheme_separator + 1, pre_separator - scheme_separator - 1);
            if (topic.empty())
            {
                throw std::invalid_argument("source target is empty: " + std::string{input});
            }

            pre_capacity = parse_size(input.substr(pre_separator + 1, last_separator - pre_separator - 1),
                                      "source pre capacity");
            post_capacity = parse_size(input.substr(last_separator + 1), "source post capacity");
        }

        Ros1TopicConfig config{std::string{topic}, pre_capacity, post_capacity};
        config.validate();

        return config;
    }

    std::vector<Ros1TopicConfig> parse_ros1_topic_configs(const std::vector<std::string>& inputs,
                                                          const FlightRecorderOptions& recorder_options)
    {
        recorder_options.validate();

        std::vector<Ros1TopicConfig> topics;
        topics.reserve(inputs.size());

        for (const auto& input : inputs)
        {
            topics.push_back(parse_ros1_topic_config(
                input,
                recorder_options.pre_capacity,
                recorder_options.post_capacity));
        }

        return topics;
    }

    Ros1DynamicRecorderCliOptions parse_ros1_dynamic_recorder_cli(CLI::App& app, int argc, char* argv[])
    {
        FlightRecorderOptions recorder;
        Ros1BridgeCliOptions  ros1;
        std::string           source_arg;

        recorder.output_path = "./logs/ros1_dynamic";

        app.set_help_flag("--help", "Print this help message and exit");
        app.add_option("--recorder.output", recorder.output_path, "Output path and file prefix");
        app.add_option("--recorder.pre-capacity", recorder.pre_capacity, "Default pre-trigger record capacity per topic");
        app.add_option("--recorder.post-capacity", recorder.post_capacity, "Default post-trigger record capacity per topic");
        app.add_option("--recorder.post-trigger-timeout-ms", recorder.post_trigger_timeout_ms, "Post-trigger wait timeout in milliseconds");
        app.add_option("--ros1.spinner-threads", ros1.spinner_threads, "ROS1 async spinner threads");
        app.add_option("--ros1.queue-size", ros1.queue_size, "ROS1 subscriber queue size");
        app.add_option("--ros1.sources", source_arg, "ROS1 topics")
            ->required();

        app.parse(argc, argv);

        Ros1DynamicRecorderCliOptions options;

        ros1.sources = split_sources(source_arg);
        recorder.mcap_metadata["source_kind"] = "ros1_dynamic";
        recorder.validate();
        ros1.validate();

        options.recorder_options = std::move(recorder);
        options.ros1_options     = ros1;
        options.topics           = parse_ros1_topic_configs(ros1.sources, options.recorder_options);

        return options;
    }

    Ros1DynamicRecorderCliOptions parse_ros1_dynamic_recorder_cli(int argc, char* argv[])
    {
        CLI::App app{"ROS1 dynamic MCAP recorder"};

        try
        {
            return parse_ros1_dynamic_recorder_cli(app, argc, argv);
        }
        catch (const CLI::ParseError& error)
        {
            throw std::invalid_argument(error.what());
        }
    }

}  // namespace flightLogger
