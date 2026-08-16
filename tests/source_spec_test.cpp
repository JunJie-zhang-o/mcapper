#include <stdexcept>
#include <string>
#include <vector>

#include "ros1/cli.hpp"

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

    void expect_invalid_cli(std::vector<std::string> args, const std::string& message)
    {
        auto argv = argv_for(args);

        expect_invalid_argument(
            [&] { (void)flightLogger::parse_ros1_dynamic_recorder_cli(static_cast<int>(argv.size()), argv.data()); },
            message);
    }

    void parses_ros1_source_with_default_capacities()
    {
        const auto spec = flightLogger::parse_ros1_topic_config("ros1:/imu/data", 12, 4);

        expect(spec.topic == "/imu/data", "unexpected source topic");
        expect(spec.pre_capacity == 12, "unexpected default pre capacity");
        expect(spec.post_capacity == 4, "unexpected default post capacity");
    }

    void parses_ros1_source_with_capacity_override()
    {
        const auto spec = flightLogger::parse_ros1_topic_config("ros1:/imu/data:2048:256", 12, 4);

        expect(spec.topic == "/imu/data", "unexpected source topic");
        expect(spec.pre_capacity == 2048, "unexpected source pre capacity");
        expect(spec.post_capacity == 256, "unexpected source post capacity");
    }

    void rejects_missing_or_unsupported_scheme()
    {
        expect_invalid_argument(
            [] { (void)flightLogger::parse_ros1_topic_config("/imu/data", 12, 4); },
            "missing scheme was accepted");
        expect_invalid_argument(
            [] { (void)flightLogger::parse_ros1_topic_config("ros2:/imu/data", 12, 4); },
            "unsupported ros2 scheme was accepted");
        expect_invalid_argument(
            [] { (void)flightLogger::parse_ros1_topic_config("zmq:tcp://127.0.0.1:5555", 12, 4); },
            "unsupported zmq scheme was accepted");
    }

    void rejects_invalid_source_capacities()
    {
        expect_invalid_argument(
            [] { (void)flightLogger::parse_ros1_topic_config("ros1:/imu/data:2048", 12, 4); },
            "missing post capacity was accepted");
        expect_invalid_argument(
            [] { (void)flightLogger::parse_ros1_topic_config("ros1:/imu/data:0:256", 12, 4); },
            "zero source pre capacity was accepted");
        expect_invalid_argument(
            [] { (void)flightLogger::parse_ros1_topic_config("ros1:/imu/data:abc:256", 12, 4); },
            "malformed source pre capacity was accepted");
        expect_invalid_argument(
            [] { (void)flightLogger::parse_ros1_topic_config("ros1:/imu/data:1:2:3", 12, 4); },
            "source topic with colon was accepted");
    }

    void parses_multiple_sources()
    {
        const std::vector<std::string> inputs{"ros1:/imu/data:2048:256", "ros1:/tf"};
        flightLogger::FlightRecorderOptions recorder;
        recorder.pre_capacity  = 12;
        recorder.post_capacity = 4;

        const auto specs = flightLogger::parse_ros1_topic_configs(inputs, recorder);

        expect(specs.size() == 2, "unexpected source count");
        expect(specs[0].topic == "/imu/data", "unexpected first source");
        expect(specs[0].pre_capacity == 2048, "unexpected first pre capacity");
        expect(specs[0].post_capacity == 256, "unexpected first post capacity");
        expect(specs[1].topic == "/tf", "unexpected second source");
        expect(specs[1].pre_capacity == 12, "unexpected second pre capacity");
        expect(specs[1].post_capacity == 4, "unexpected second post capacity");
    }

    void validates_cli_option_structs()
    {
        flightLogger::FlightRecorderOptions recorder;
        recorder.validate();

        recorder.pre_capacity = 0;
        expect_invalid_argument(
            [&] { recorder.validate(); },
            "recorder accepted zero pre capacity");

        flightLogger::Ros1BridgeCliOptions ros1;
        ros1.sources = {"ros1:/imu/data"};
        ros1.validate();

        ros1.spinner_threads = 0;
        expect_invalid_argument(
            [&] { ros1.validate(); },
            "ros1 options accepted zero spinner threads");

        flightLogger::Ros1TopicConfig topic{"/imu/data", 12, 4};
        topic.validate();

        topic.post_capacity = 0;
        expect_invalid_argument(
            [&] { topic.validate(); },
            "topic config accepted zero post capacity");
    }

    void dynamic_cli_parses_nested_options()
    {
        std::vector<std::string> args{
            "ros1_dynamic_recorder",
            "--recorder.output=./logs/run01",
            "--recorder.pre-capacity=12",
            "--recorder.post-capacity=4",
            "--recorder.post-trigger-timeout-ms=250",
            "--ros1.spinner-threads=8",
            "--ros1.queue-size=16",
            "--ros1.sources=ros1:/imu/data:2048:256,ros1:/tf",
        };
        auto argv = argv_for(args);

        const auto options = flightLogger::parse_ros1_dynamic_recorder_cli(static_cast<int>(argv.size()), argv.data());

        expect(options.recorder_options.output_path == "./logs/run01", "unexpected output path");
        expect(options.recorder_options.pre_capacity == 12, "unexpected pre capacity");
        expect(options.recorder_options.post_capacity == 4, "unexpected post capacity");
        expect(options.recorder_options.post_trigger_timeout_ms == 250, "unexpected post trigger timeout");
        expect(options.ros1_options.spinner_threads == 8, "unexpected spinner threads");
        expect(options.ros1_options.queue_size == 16, "unexpected queue size");
        expect(options.topics.size() == 2, "unexpected topic count");
        expect(options.topics[0].pre_capacity == 2048, "unexpected topic pre capacity override");
        expect(options.topics[0].post_capacity == 256, "unexpected topic post capacity override");
        expect(options.topics[1].pre_capacity == 12, "unexpected default topic pre capacity");
        expect(options.topics[1].post_capacity == 4, "unexpected default topic post capacity");
    }

    void dynamic_cli_uses_nested_defaults()
    {
        std::vector<std::string> args{
            "ros1_dynamic_recorder",
            "--ros1.sources=ros1:/imu/data",
        };
        auto argv = argv_for(args);

        const auto options = flightLogger::parse_ros1_dynamic_recorder_cli(static_cast<int>(argv.size()), argv.data());

        expect(options.recorder_options.output_path == "./logs/ros1_dynamic", "unexpected default output path");
        expect(options.recorder_options.pre_capacity == 4096, "unexpected default pre capacity");
        expect(options.recorder_options.post_capacity == 4096, "unexpected default post capacity");
        expect(options.recorder_options.post_trigger_timeout_ms == 1000, "unexpected default post trigger timeout");
        expect(options.ros1_options.spinner_threads == 4, "unexpected default spinner threads");
        expect(options.ros1_options.queue_size == 5, "unexpected default queue size");
        expect(options.topics[0].pre_capacity == 4096, "unexpected default topic pre capacity");
        expect(options.topics[0].post_capacity == 4096, "unexpected default topic post capacity");
    }

    void dynamic_cli_rejects_invalid_nested_options()
    {
        expect_invalid_cli(
            {"ros1_dynamic_recorder", "--ros1.sources=ros1:/imu/data", "--recorder.pre-capacity=0"},
            "zero pre capacity was accepted");

        expect_invalid_cli(
            {"ros1_dynamic_recorder", "--ros1.sources=ros1:/imu/data", "--recorder.pre-capacity=-1"},
            "negative pre capacity was accepted");

        expect_invalid_cli(
            {"ros1_dynamic_recorder", "--ros1.sources=ros1:/imu/data", "--ros1.spinner-threads=0"},
            "zero spinner threads was accepted");

        expect_invalid_cli(
            {"ros1_dynamic_recorder", "--ros1.sources=ros1:/imu/data", "--ros1.spinner-threads=-1"},
            "negative spinner threads was accepted");

        expect_invalid_cli(
            {"ros1_dynamic_recorder", "--ros1.sources=ros1:/imu/data", "--ros1.queue-size=0"},
            "zero queue size was accepted");

        expect_invalid_cli(
            {"ros1_dynamic_recorder", "--ros1.sources=ros1:/imu/data", "--ros1.queue-size=abc"},
            "malformed queue size was accepted");

        expect_invalid_cli(
            {"ros1_dynamic_recorder", "--ros1.sources=ros1:/imu/data", "--ros1.queue-size=4294967296"},
            "oversized queue size was accepted");

        expect_invalid_cli(
            {"ros1_dynamic_recorder", "--ros1.sources=ros1:/imu/data", "--recorder.post-trigger-timeout-ms=-1"},
            "negative post trigger timeout was accepted");
    }

    void dynamic_cli_rejects_missing_sources_and_old_style_args()
    {
        expect_invalid_cli(
            {"ros1_dynamic_recorder"},
            "missing sources were accepted");

        expect_invalid_cli(
            {"ros1_dynamic_recorder", "--output=./logs/run01", "--ros1.sources=ros1:/imu/data"},
            "old output option was accepted");

        expect_invalid_cli(
            {"ros1_dynamic_recorder", "--ros1.sources=ros1:/imu/data", "ros1:/tf"},
            "old positional source was accepted");

        expect_invalid_cli(
            {"ros1_dynamic_recorder", "-h", "--ros1.sources=ros1:/imu/data"},
            "short option was accepted");
    }
}  // namespace

int main()
{
    try
    {
        parses_ros1_source_with_default_capacities();
        parses_ros1_source_with_capacity_override();
        rejects_missing_or_unsupported_scheme();
        rejects_invalid_source_capacities();
        parses_multiple_sources();
        validates_cli_option_structs();
        dynamic_cli_parses_nested_options();
        dynamic_cli_uses_nested_defaults();
        dynamic_cli_rejects_invalid_nested_options();
        dynamic_cli_rejects_missing_sources_and_old_style_args();
    }
    catch (const std::exception& error)
    {
        fail(error.what());
    }

    return 0;
}
