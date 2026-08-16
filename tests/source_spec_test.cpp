#include <array>
#include <stdexcept>
#include <string>
#include <vector>

#include <rfl/cli.hpp>

#include "cli.hpp"

namespace
{
    void fail(const std::string& message)
    {
        throw std::runtime_error(message);
    }

    void expect(bool condition, const std::string& message)
    {
        if (!condition) fail(message);
    }

    template <typename Func>
    void expect_invalid_argument(Func func, const std::string& message)
    {
        try
        {
            func();
        }
        catch (const std::invalid_argument&)
        {
            return;
        }

        fail(message);
    }

    std::vector<char*> argv_for(std::vector<std::string>& args)
    {
        std::vector<char*> argv;
        argv.reserve(args.size());
        for (auto& arg : args) argv.push_back(arg.data());
        return argv;
    }

    void parses_ros1_source()
    {
        const auto spec = flightLogger::parse_source_spec("ros1:/imu/data");

        expect(spec.scheme == "ros1", "unexpected source scheme");
        expect(spec.target == "/imu/data", "unexpected source target");
    }

    void rejects_missing_or_unsupported_scheme()
    {
        expect_invalid_argument(
            [] { (void)flightLogger::parse_source_spec("/imu/data"); },
            "missing scheme was accepted");
        expect_invalid_argument(
            [] { (void)flightLogger::parse_source_spec("ros2:/imu/data"); },
            "unsupported ros2 scheme was accepted");
        expect_invalid_argument(
            [] { (void)flightLogger::parse_source_spec("zmq:tcp://127.0.0.1:5555"); },
            "unsupported zmq scheme was accepted");
    }

    void parses_multiple_sources()
    {
        const std::array<std::string, 2> inputs{"ros1:/imu/data", "ros1:/tf"};
        const auto                       specs = flightLogger::parse_source_specs(inputs);

        expect(specs.size() == 2, "unexpected source count");
        expect(specs[0].target == "/imu/data", "unexpected first source");
        expect(specs[1].target == "/tf", "unexpected second source");
    }

    void reflect_cpp_parse_argv_keeps_multiple_positionals()
    {
        std::vector<std::string> args{
            "ros1_dynamic_recorder",
            "--output=./logs/run01",
            "ros1:/imu/data",
            "ros1:/tf",
        };
        auto argv = argv_for(args);

        auto parsed = rfl::cli::parse_argv(static_cast<int>(argv.size()), argv.data());
        if (!parsed) fail(parsed.error().what());

        expect(parsed->named.at("output") == "./logs/run01", "unexpected parsed output");
        expect(parsed->positional.size() == 2, "unexpected positional count");
        expect(parsed->positional[0] == "ros1:/imu/data", "unexpected first positional");
        expect(parsed->positional[1] == "ros1:/tf", "unexpected second positional");
    }

    void dynamic_cli_splits_output()
    {
        std::vector<std::string> args{
            "ros1_dynamic_recorder",
            "--output=./logs/run01",
            "ros1:/imu/data",
        };
        auto argv = argv_for(args);

        const auto options = flightLogger::parse_dynamic_recorder_cli(static_cast<int>(argv.size()), argv.data());

        expect(options.recorder_options.output_path == "./logs/", "unexpected output path");
        expect(options.recorder_options.output_file_name == "run01", "unexpected output file name");
    }
}  // namespace

int main()
{
    try
    {
        parses_ros1_source();
        rejects_missing_or_unsupported_scheme();
        parses_multiple_sources();
        reflect_cpp_parse_argv_keeps_multiple_positionals();
        dynamic_cli_splits_output();
    }
    catch (const std::exception& error)
    {
        fail(error.what());
    }

    return 0;
}
