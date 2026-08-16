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

#include "CLI11.hpp"
#include "bridge/ros1/ros1.hpp"
#include "recorder.hpp"
#include "ros1/cli.hpp"

namespace
{
    volatile std::sig_atomic_t g_shutdown_signal = 0;
    volatile std::sig_atomic_t g_custom_trigger_signal = 0;

    void shutdown_signal_handler(int signum)
    {
        g_shutdown_signal = signum;
    }

    void custom_trigger_signal_handler(int signum)
    {
        g_custom_trigger_signal = signum;
    }

    void print_topics(const std::string& label, const std::vector<std::string>& topics)
    {
        if (topics.empty()) return;

        std::cerr << label;
        for (const auto& topic : topics) std::cerr << ' ' << topic;
        std::cerr << '\n';
    }

    const char* recorder_state_name(flightLogger::RecorderState state) noexcept
    {
        switch (state)
        {
            case flightLogger::RecorderState::Idle:
                return "Idle";
            case flightLogger::RecorderState::Armed:
                return "Armed";
            case flightLogger::RecorderState::FreezingPreTrigger:
                return "FreezingPreTrigger";
            case flightLogger::RecorderState::PostTrigger:
                return "PostTrigger";
            case flightLogger::RecorderState::FreezingPostTrigger:
                return "FreezingPostTrigger";
            case flightLogger::RecorderState::Dumping:
                return "Dumping";
            case flightLogger::RecorderState::Finalizing:
                return "Finalizing";
        }

        return "Unknown";
    }
}  // namespace

int main(int argc, char** argv)
{
    CLI::App app{"ROS1 dynamic MCAP recorder"};

    try
    {
        ros::init(argc, argv, "ros1_dynamic_recorder", ros::init_options::NoSigintHandler);

        std::vector<std::string> clean_args;
        ros::removeROSArgs(argc, argv, clean_args);
        std::vector<char*> clean_argv;
        clean_argv.reserve(clean_args.size()); 
        for (auto& arg : clean_args) clean_argv.push_back(arg.data());

        auto cli_options = flightLogger::parse_ros1_dynamic_recorder_cli(app, static_cast<int>(clean_argv.size()), clean_argv.data());

        auto output_dir = std::filesystem::path{cli_options.recorder_options.output_path}.parent_path();
        if (output_dir.empty()) output_dir = ".";
        std::filesystem::create_directories(output_dir);

        ros::NodeHandle node;
        flightLogger::FlightRecorder recorder{cli_options.recorder_options};
        flightLogger::Ros1TopicBridge bridge{
            node,
            recorder,
            std::move(cli_options.topics),
            flightLogger::Ros1TopicBridgeOptions{cli_options.ros1_options.queue_size}};
        bridge.start();

        std::signal(SIGINT, shutdown_signal_handler);
        std::signal(SIGTERM, shutdown_signal_handler);
        std::signal(SIGUSR1, custom_trigger_signal_handler);

        ros::AsyncSpinner spinner{cli_options.ros1_options.spinner_threads};
        spinner.start();

        std::cerr << "[mcapper] recording ROS1 sources; send SIGUSR1 to trigger MCAP dump, Ctrl+C to dump and exit\n";
        print_topics("[mcapper] waiting for first message on:", bridge.pending_topics());

        while (ros::ok() && !g_shutdown_signal)
        {
            if (g_custom_trigger_signal)
            {
                const auto signum = g_custom_trigger_signal;
                g_custom_trigger_signal = 0;
                const auto state = recorder.state();

                if (state != flightLogger::RecorderState::Armed)
                {
                    const std::string reason = signum == SIGUSR1 ? "SIGUSR1" : "custom";
                    std::cerr << "[mcapper] trigger ignored: " << reason
                              << " cannot generate MCAP while recorder state is "
                              << recorder_state_name(state) << '\n';
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }

                const auto pending = bridge.pending_topics();
                if (!pending.empty()) print_topics("[mcapper] no schema received before trigger for:", pending);

                const std::string reason = signum == SIGUSR1 ? "SIGUSR1" : "custom";
                std::cerr << "[mcapper] trigger: " << reason << '\n';
                recorder.trigger(flightLogger::ros1_now_ns(), reason);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        while (recorder.state() != flightLogger::RecorderState::Armed) std::this_thread::sleep_for(std::chrono::milliseconds(100));

        const auto pending = bridge.pending_topics();
        if (!pending.empty()) print_topics("[mcapper] no schema received before trigger for:", pending);

        const std::string reason = g_shutdown_signal == SIGTERM ? "SIGTERM" : "SIGINT";
        std::cerr << "[mcapper] trigger: " << reason << '\n';
        recorder.trigger(flightLogger::ros1_now_ns(), reason);
        recorder.stop();

        spinner.stop();

        print_topics("[mcapper] recorded topics:", bridge.ready_topics());
        std::cerr << "[mcapper] output prefix: " << std::filesystem::absolute(cli_options.recorder_options.output_path) << '\n';
    }
    catch (const CLI::ParseError& error)
    {
        return app.exit(error);
    }
    catch (const std::exception& error)
    {
        std::cerr << "ros1_dynamic_recorder failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
