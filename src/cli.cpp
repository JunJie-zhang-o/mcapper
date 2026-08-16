#include "cli.hpp"

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace flightLogger
{
    namespace
    {
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

    void RecorderCliOptions::validate() const
    {
        if (pre_capacity == 0)
        {
            throw std::invalid_argument("invalid positive integer value for --recorder.pre-capacity");
        }
        if (post_capacity == 0)
        {
            throw std::invalid_argument("invalid positive integer value for --recorder.post-capacity");
        }
        if (post_trigger_timeout_sec < 0.0)
        {
            throw std::invalid_argument("invalid non-negative numeric value for --recorder.post-trigger-timeout-sec");
        }
    }

    FlightRecorderOptions make_flight_recorder_options(const RecorderCliOptions& cli_options,
                                                       std::string_view source_kind)
    {
        cli_options.validate();

        FlightRecorderOptions options;
        options.pre_capacity            = cli_options.pre_capacity;
        options.post_capacity           = cli_options.post_capacity;
        options.post_trigger_timeout_ns = static_cast<uint64_t>(cli_options.post_trigger_timeout_sec * 1'000'000'000.0);
        apply_output_arg(options, cli_options.output);
        options.mcap_metadata["source_kind"] = std::string{source_kind};
        return options;
    }

}  // namespace flightLogger
