#pragma once

#ifndef FLIGHTLOGGER_ENABLE_ROS1
#define FLIGHTLOGGER_ENABLE_ROS1 0
#endif

#if FLIGHTLOGGER_ENABLE_ROS1

#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <ros/ros.h>
#include <topic_tools/shape_shifter.h>

#include "channel.hpp"
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

    template <std::size_t N>
    class Ros1TopicBridge
    {
    public:
        Ros1TopicBridge(ros::NodeHandle& node, FlightRecorder& recorder, std::vector<std::string> topics)
            : node_(node), recorder_(recorder)
        {
            this->topics_.reserve(topics.size());
            for (auto& topic : topics)
            {
                if (topic.empty()) throw std::invalid_argument("ros1 topic is empty");
                this->topics_.push_back(std::make_unique<TopicState>(std::move(topic)));
            }
        }

        void start()
        {
            for (auto& topic : this->topics_)
            {
                TopicState* state = topic.get();
                state->subscriber = this->node_.subscribe<topic_tools::ShapeShifter>(
                    state->topic,
                    100,
                    [this, state](const topic_tools::ShapeShifter::ConstPtr& message)
                    {
                        this->handle_message(*state, message);
                    });
            }
        }

        std::vector<std::string> ready_topics() const
        {
            std::lock_guard<std::mutex> lock(this->mutex_);
            std::vector<std::string>    result;

            for (const auto& topic : this->topics_)
            {
                if (topic->registered) result.push_back(topic->topic);
            }

            return result;
        }

        std::vector<std::string> pending_topics() const
        {
            std::lock_guard<std::mutex> lock(this->mutex_);
            std::vector<std::string>    result;

            for (const auto& topic : this->topics_)
            {
                if (!topic->registered) result.push_back(topic->topic);
            }

            return result;
        }

    private:
        struct TopicState
        {
            explicit TopicState(std::string topic_name) : topic(std::move(topic_name))
            {
            }

            std::string topic;
            TripleRingBuffer<TimedRecord<RawRos1Message>, N> ring;
            ros::Subscriber subscriber;
            bool registered{false};
            bool registration_failed{false};
        };

        void handle_message(TopicState& state, const topic_tools::ShapeShifter::ConstPtr& message)
        {
            if (!message) return;

            const auto timestamp_ns = ros1_now_ns();
            RawRos1Message raw{serialize_raw_ros1_message(*message)};

            std::lock_guard<std::mutex> lock(this->mutex_);

            if (!state.registered && !state.registration_failed)
            {
                ChannelInfo info;
                info.topic             = state.topic;
                info.message_encoding  = "ros1";
                info.schema_name       = message->getDataType();
                info.schema_encoding   = "ros1msg";
                info.schema_data       = detail::ros1_schema_data(message->getMessageDefinition());
                info.metadata["topic"] = state.topic;
                info.metadata["md5sum"] = message->getMD5Sum();

                try
                {
                    this->recorder_.register_channel<RawRos1Message, N>(
                        std::move(info),
                        state.ring,
                        std::make_unique<RawRos1MessageSerializer>());
                    state.registered = true;
                }
                catch (const std::exception& error)
                {
                    state.registration_failed = true;
                    ROS_ERROR_STREAM("failed to register ROS1 topic " << state.topic << ": " << error.what());
                    return;
                }
            }

            if (!state.registered) return;

            state.ring.push(TimedRecord<RawRos1Message>{timestamp_ns, std::move(raw)});
        }

        ros::NodeHandle&                         node_;
        FlightRecorder&                          recorder_;
        std::vector<std::unique_ptr<TopicState>> topics_;
        mutable std::mutex                       mutex_;
    };

}  // namespace flightLogger

#endif
