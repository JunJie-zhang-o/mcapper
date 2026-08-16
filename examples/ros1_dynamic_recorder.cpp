#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <ros/ros.h>

#include "bridge/ros1.hpp"
#include "cli.hpp"
#include "recorder.hpp"

namespace
{
    volatile std::sig_atomic_t g_shutdown_signal = 0;

    void signal_handler(int signum)
    {
        g_shutdown_signal = signum;
    }

    void print_topics(const std::string& label, const std::vector<std::string>& topics)
    {
        if (topics.empty()) return;

        std::cerr << label;
        for (const auto& topic : topics) std::cerr << ' ' << topic;
        std::cerr << '\n';
    }
}  // namespace

int main(int argc, char** argv)
{
    try
    {
        ros::init(argc, argv, "ros1_dynamic_recorder", ros::init_options::NoSigintHandler);

        std::vector<std::string> clean_args;
        ros::removeROSArgs(argc, argv, clean_args);
        std::vector<char*> clean_argv;
        clean_argv.reserve(clean_args.size());
        for (auto& arg : clean_args) clean_argv.push_back(arg.data());

        auto cli_options = flightLogger::parse_dynamic_recorder_cli(static_cast<int>(clean_argv.size()), clean_argv.data());

        if (cli_options.help)
        {
            std::cerr << flightLogger::dynamic_recorder_usage(argv[0]);
            return 0;
        }

        std::vector<std::string> ros1_topics;
        ros1_topics.reserve(cli_options.sources.size());
        for (const auto& spec : cli_options.sources) ros1_topics.push_back(spec.target);

        constexpr std::size_t kRingSize = 4096;

        std::filesystem::create_directories(cli_options.recorder_options.output_path);

        ros::NodeHandle node;
        flightLogger::FlightRecorder recorder{cli_options.recorder_options};
        flightLogger::Ros1TopicBridge<kRingSize> bridge{node, recorder, std::move(ros1_topics)};
        bridge.start();

        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        ros::AsyncSpinner spinner{1};
        spinner.start();

        std::cerr << "[mcapper] recording ROS1 sources; press Ctrl+C to trigger MCAP dump\n";
        print_topics("[mcapper] waiting for first message on:", bridge.pending_topics());

        while (ros::ok() && !g_shutdown_signal) std::this_thread::sleep_for(std::chrono::milliseconds(100));

        const auto pending = bridge.pending_topics();
        if (!pending.empty()) print_topics("[mcapper] no schema received before trigger for:", pending);

        const std::string reason = g_shutdown_signal == SIGTERM ? "SIGTERM" : "SIGINT";
        std::cerr << "[mcapper] trigger: " << reason << '\n';
        recorder.trigger(flightLogger::ros1_now_ns(), reason);
        recorder.stop();

        spinner.stop();

        print_topics("[mcapper] recorded topics:", bridge.ready_topics());
        std::cerr << "[mcapper] output directory: " << std::filesystem::absolute(cli_options.recorder_options.output_path) << '\n';
    }
    catch (const std::exception& error)
    {
        std::cerr << flightLogger::dynamic_recorder_usage(argv[0]);
        std::cerr << "ros1_dynamic_recorder failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
