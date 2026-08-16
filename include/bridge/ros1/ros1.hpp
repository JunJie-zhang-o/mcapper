#pragma once

#ifndef FLIGHTLOGGER_ENABLE_ROS1
#define FLIGHTLOGGER_ENABLE_ROS1 0
#endif

#if FLIGHTLOGGER_ENABLE_ROS1

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <ros/ros.h>
#include <topic_tools/shape_shifter.h>

#include "channel.hpp"
#include "bridge/ros1/config.hpp"
#include "codec/ros1.hpp"
#include "recorder.hpp"

namespace flightLogger
{

    struct RawRos1Message
    {
        SerializedPayload data;
    };

    class RawRos1MessageSerializer final : public ISerializer<RawRos1Message>
    {
    public:
        SerializedPayload serialize(const RawRos1Message& value) override;
    };

    uint64_t ros1_time_to_ns(const ros::Time& time) noexcept;
    uint64_t ros1_now_ns();
    SerializedPayload serialize_raw_ros1_message(const topic_tools::ShapeShifter& message);

    struct Ros1TopicBridgeOptions
    {
        uint32_t queue_size{5};
    };

    class Ros1TopicBridge
    {
    private:
        enum class RegistrationState
        {
            Pending,
            Registering,
            Registered,
            Failed,
        };

    public:
        Ros1TopicBridge(ros::NodeHandle& node,
                        FlightRecorder& recorder,
                        std::vector<Ros1TopicConfig> topics,
                        Ros1TopicBridgeOptions options = {})
            : node_(node), recorder_(recorder), options_(options)
        {
            if (this->options_.queue_size == 0) throw std::invalid_argument("ros1 queue size must be greater than zero");

            this->topics_.reserve(topics.size());
            for (auto& topic_config : topics)
            {
                if (topic_config.topic.empty()) throw std::invalid_argument("ros1 topic is empty");
                this->topics_.push_back(std::make_unique<TopicState>(std::move(topic_config)));
            }
        }

        void start()
        {
            for (auto& topic : this->topics_)
            {
                TopicState* state = topic.get();
                state->subscriber = this->node_.subscribe<topic_tools::ShapeShifter>(
                    state->topic,
                    this->options_.queue_size,
                    [this, state](const topic_tools::ShapeShifter::ConstPtr& message)
                    {
                        this->handle_message(*state, message);
                    });
            }
        }

        std::vector<std::string> ready_topics() const
        {
            std::vector<std::string> result;

            for (const auto& topic : this->topics_)
            {
                if (topic->registration_state.load(std::memory_order_acquire) == RegistrationState::Registered)
                    result.push_back(topic->topic);
            }

            return result;
        }

        std::vector<std::string> pending_topics() const
        {
            std::vector<std::string> result;

            for (const auto& topic : this->topics_)
            {
                if (topic->registration_state.load(std::memory_order_acquire) != RegistrationState::Registered)
                    result.push_back(topic->topic);
            }

            return result;
        }

    private:
        struct TopicState
        {
            explicit TopicState(Ros1TopicConfig config)
                : topic(std::move(config.topic)),
                  pre_capacity(config.pre_capacity),
                  post_capacity(config.post_capacity),
                  ring(pre_capacity, post_capacity)
            {
            }

            std::string topic;
            std::size_t pre_capacity;
            std::size_t post_capacity;
            BlackBox<TimedRecord<RawRos1Message>> ring;
            ros::Subscriber subscriber;
            std::atomic<RegistrationState> registration_state{RegistrationState::Pending};
        };

        bool register_topic(TopicState& state, const topic_tools::ShapeShifter& message)
        {
            ChannelInfo info;
            info.topic             = state.topic;
            info.message_encoding  = "ros1";
            info.schema_name       = message.getDataType();
            info.schema_encoding   = "ros1msg";
            info.schema_data       = detail::ros1_schema_data(message.getMessageDefinition());
            info.metadata["topic"] = state.topic;
            info.metadata["md5sum"] = message.getMD5Sum();
            info.metadata["pre_capacity"]  = std::to_string(state.pre_capacity);
            info.metadata["post_capacity"] = std::to_string(state.post_capacity);

            try
            {
                this->recorder_.register_channel<RawRos1Message>(
                    std::move(info),
                    state.ring,
                    std::make_unique<RawRos1MessageSerializer>());
                return true;
            }
            catch (const std::exception& error)
            {
                ROS_ERROR_STREAM("failed to register ROS1 topic " << state.topic << ": " << error.what());
                return false;
            }
        }

        void handle_message(TopicState& state, const topic_tools::ShapeShifter::ConstPtr& message)
        {
            if (!message) return;

            const auto timestamp_ns = ros1_now_ns();
            RawRos1Message raw{serialize_raw_ros1_message(*message)};

            auto registration_state = state.registration_state.load(std::memory_order_acquire);
            if (registration_state == RegistrationState::Pending)
            {
                auto expected = RegistrationState::Pending;
                if (state.registration_state.compare_exchange_strong(
                        expected,
                        RegistrationState::Registering,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire))
                {
                    const auto next_state =
                        this->register_topic(state, *message) ? RegistrationState::Registered : RegistrationState::Failed;
                    state.registration_state.store(next_state, std::memory_order_release);
                    registration_state = next_state;
                }
                else
                {
                    registration_state = expected;
                }
            }

            while (registration_state == RegistrationState::Registering)
            {
                std::this_thread::yield();
                registration_state = state.registration_state.load(std::memory_order_acquire);
            }

            if (registration_state != RegistrationState::Registered) return;

            state.ring.push(TimedRecord<RawRos1Message>{timestamp_ns, std::move(raw)});
        }

        ros::NodeHandle&                         node_;
        FlightRecorder&                          recorder_;
        Ros1TopicBridgeOptions                   options_;
        std::vector<std::unique_ptr<TopicState>> topics_;
    };

}  // namespace flightLogger

#endif
