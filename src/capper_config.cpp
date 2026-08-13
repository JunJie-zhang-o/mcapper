#include <mcapper/capper_config.hpp>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <yaml-cpp/yaml.h>

namespace mcapper
{

    namespace
    {

        void applyLoggerConfig(const YAML::Node& node, LoggerOptions& options)
        {
            if (!node)
            {
                return;
            }

            if (node["output_directory"])
            {
                options.output_directory = node["output_directory"].as<std::string>();
            }
            if (node["base_filename"])
            {
                options.base_filename = node["base_filename"].as<std::string>();
            }
            if (node["ring_buffer_capacity"])
            {
                options.ring_buffer_capacity = node["ring_buffer_capacity"].as<std::size_t>();
            }
            if (node["pre_trigger_duration_ns"])
            {
                options.pre_trigger_duration_ns = node["pre_trigger_duration_ns"].as<std::uint64_t>();
            }
            if (node["post_trigger_duration_ns"])
            {
                options.post_trigger_duration_ns = node["post_trigger_duration_ns"].as<std::uint64_t>();
            }
        }

        SourceType parseSourceType(const std::string& value)
        {
            if (value == "ros1")
            {
                return SourceType::Ros1;
            }
            throw std::invalid_argument("unsupported source type: " + value);
        }

    }  // namespace

    CapperConfig loadYamlConfig(const std::string& path)
    {
        const auto root = YAML::LoadFile(path);

        CapperConfig config;
        applyLoggerConfig(root["logger"], config.logger);

        const auto source = root["source"];
        if (!source)
        {
            throw std::invalid_argument("source config is required");
        }
        if (!source["type"])
        {
            throw std::invalid_argument("source.type is required");
        }
        config.source.type = parseSourceType(source["type"].as<std::string>());

        const auto topics = source["topics"];
        if (!topics || !topics.IsSequence() || topics.size() == 0)
        {
            throw std::invalid_argument("source.topics must be a non-empty sequence");
        }

        config.source.topics.clear();
        for (const auto& topic : topics)
        {
            const auto value = topic.as<std::string>();
            if (value.empty())
            {
                throw std::invalid_argument("source.topics must not contain empty topic names");
            }
            config.source.topics.push_back(value);
        }

        return config;
    }

}  // namespace mcapper
