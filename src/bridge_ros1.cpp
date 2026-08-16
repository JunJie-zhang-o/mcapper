#include "bridge/ros1/ros1.hpp"

#if FLIGHTLOGGER_ENABLE_ROS1

#include <cstdint>

#include <ros/serialization.h>

namespace flightLogger
{

    SerializedPayload RawRos1MessageSerializer::serialize(const RawRos1Message& value)
    {
        return value.data;
    }

    uint64_t ros1_time_to_ns(const ros::Time& time) noexcept
    {
        return static_cast<uint64_t>(time.sec) * 1'000'000'000ULL + static_cast<uint64_t>(time.nsec);
    }

    uint64_t ros1_now_ns()
    {
        return ros1_time_to_ns(ros::Time::now());
    }

    SerializedPayload serialize_raw_ros1_message(const topic_tools::ShapeShifter& message)
    {
        const auto size = ros::serialization::serializationLength(message);

        SerializedPayload out(size);
        if (out.empty()) return out;

        ros::serialization::OStream stream(
            reinterpret_cast<std::uint8_t*>(out.data()),
            static_cast<std::uint32_t>(out.size()));
        ros::serialization::serialize(stream, message);
        return out;
    }

}  // namespace flightLogger

#endif
