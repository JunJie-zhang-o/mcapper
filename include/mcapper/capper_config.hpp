#pragma once

#include <mcapper/logger_options.hpp>
#include <string>
#include <vector>

namespace mcapper
{

    enum class SourceType
    {
        Ros1,
    };

    struct SourceConfig
    {
        SourceType              type{SourceType::Ros1};
        std::vector<std::string> topics;
    };

    struct CapperConfig
    {
        LoggerOptions logger;
        SourceConfig  source;
    };

    CapperConfig loadYamlConfig(const std::string& path);

}  // namespace mcapper
