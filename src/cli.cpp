#include "cli.hpp"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

#include <rfl/cli.hpp>

namespace flightLogger
{
    namespace
    {
        constexpr uint64_t kNsPerSecond = 1'000'000'000ULL;

        std::string named_or(const rfl::cli::ParsedArgs& args, const std::string& key, std::string fallback)
        {
            const auto it = args.named.find(key);
            if (it == args.named.end() || it->second.empty()) return fallback;

            return it->second;
        }

        double named_double_or(const rfl::cli::ParsedArgs& args, const std::string& key, double fallback)
        {
            const auto it = args.named.find(key);
            if (it == args.named.end() || it->second.empty()) return fallback;

            std::size_t parsed = 0;
            const double value = std::stod(it->second, &parsed);
            if (parsed != it->second.size() || value < 0.0)
            {
                throw std::invalid_argument("invalid non-negative numeric value for --" + key + ": " + it->second);
            }

            return value;
        }

        std::size_t named_size_or(const rfl::cli::ParsedArgs& args, const std::string& key, std::size_t fallback)
        {
            const auto it = args.named.find(key);
            if (it == args.named.end() || it->second.empty()) return fallback;

            std::size_t       parsed = 0;
            const auto        value  = std::stoull(it->second, &parsed);
            constexpr uint64_t max_size = static_cast<uint64_t>(std::numeric_limits<std::size_t>::max());
            if (parsed != it->second.size() || value == 0 || value > max_size)
            {
                throw std::invalid_argument("invalid positive integer value for --" + key + ": " + it->second);
            }

            return static_cast<std::size_t>(value);
        }

        void reject_unknown_options(const rfl::cli::ParsedArgs& args)
        {
            const std::unordered_set<std::string> allowed{
                "help",
                "output",
                "pre-capacity",
                "post-capacity",
                "post-trigger-timeout-sec",
            };

            for (const auto& [key, value] : args.named)
            {
                (void)value;
                if (!allowed.contains(key)) throw std::invalid_argument("unknown option: --" + key);
            }

            if (!args.short_args.empty()) throw std::invalid_argument("short options are not supported");
        }

        uint64_t seconds_to_ns(double seconds)
        {
            return static_cast<uint64_t>(seconds * static_cast<double>(kNsPerSecond));
        }

        void apply_output_arg(FlightRecorderOptions& options, std::string output)
        {
            if (output.empty()) output = "./logs/ros1_dynamic";

            std::filesystem::path path{output};
            if (path.has_filename())
            {
                options.output_file_name = path.filename().string();
                path.remove_filename();
            }
            else
            {
                options.output_file_name = "ros1_dynamic";
            }

            if (options.output_file_name.empty())
            {
                throw std::invalid_argument("output file name is empty");
            }

            options.output_path = path.empty() ? "." : path.string();
        }
    }  // namespace

    SourceSpec parse_source_spec(std::string_view input)
    {
        const auto separator = input.find(':');
        if (separator == std::string_view::npos || separator == 0)
        {
            throw std::invalid_argument("source must include an explicit scheme prefix, for example ros1:/imu/data");
        }

        SourceSpec spec{
            std::string{input.substr(0, separator)},
            std::string{input.substr(separator + 1)},
        };

        if (spec.target.empty())
        {
            throw std::invalid_argument("source target is empty: " + std::string{input});
        }

        if (spec.scheme != "ros1")
        {
            throw std::invalid_argument("unsupported source scheme: " + spec.scheme);
        }

        return spec;
    }

    std::vector<SourceSpec> parse_source_specs(std::span<const std::string> inputs)
    {
        std::vector<SourceSpec> specs;
        specs.reserve(inputs.size());

        for (const auto& input : inputs) specs.push_back(parse_source_spec(input));

        return specs;
    }

    DynamicRecorderCliOptions parse_dynamic_recorder_cli(int argc, char* argv[])
    {
        auto parsed_result = rfl::cli::parse_argv(argc, argv);
        if (!parsed_result) throw std::invalid_argument(parsed_result.error().what());

        const auto parsed = *parsed_result;
        reject_unknown_options(parsed);

        DynamicRecorderCliOptions options;
        options.help = parsed.named.contains("help");
        if (options.help) return options;

        if (parsed.positional.empty())
        {
            throw std::invalid_argument("at least one source is required");
        }

        options.sources = parse_source_specs(parsed.positional);
        options.recorder_options.pre_capacity  = named_size_or(parsed, "pre-capacity", options.recorder_options.pre_capacity);
        options.recorder_options.post_capacity = named_size_or(parsed, "post-capacity", options.recorder_options.post_capacity);
        options.recorder_options.post_trigger_timeout_ns =
            seconds_to_ns(named_double_or(parsed, "post-trigger-timeout-sec", 1.0));
        apply_output_arg(options.recorder_options, named_or(parsed, "output", "./logs/ros1_dynamic"));
        options.recorder_options.mcap_metadata["source_kind"] = "ros1_dynamic";

        return options;
    }

    std::string dynamic_recorder_usage(std::string_view program)
    {
        std::string usage;
        usage += "Usage: ";
        usage += program;
        usage += " [options] ros1:/topic [ros1:/topic ...]\n";
        usage += "Options:\n";
        usage += "  --output=PATH/PREFIX        Output path and file prefix, default ./logs/ros1_dynamic\n";
        usage += "  --pre-capacity=N            Pre-trigger record capacity per topic, default 4096\n";
        usage += "  --post-capacity=N           Post-trigger record capacity per topic, default 4096\n";
        usage += "  --post-trigger-timeout-sec=SECONDS  Post-trigger wait timeout, default 1\n";
        return usage;
    }

}  // namespace flightLogger
