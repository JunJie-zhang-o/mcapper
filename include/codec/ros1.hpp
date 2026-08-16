#pragma once

#include <cstdint>
#include <string_view>

#ifndef FLIGHTLOGGER_ENABLE_ROS1
#define FLIGHTLOGGER_ENABLE_ROS1 0
#endif

#if FLIGHTLOGGER_ENABLE_ROS1
#include <ros/message_traits.h>
#include <ros/serialization.h>
#endif

#include "codec.hpp"

namespace flightLogger
{
#if FLIGHTLOGGER_ENABLE_ROS1
    namespace detail
    {
        inline SerializedPayload ros1_schema_data(std::string_view value)
        {
            if (value.empty()) return {};

            const auto* begin = reinterpret_cast<const std::byte*>(value.data());
            return {begin, begin + value.size()};
        }

        template <typename T>
        std::string ros1_message_type()
        {
            return ros::message_traits::DataType<T>::value();
        }

        template <typename T>
        std::string raw_ros1_message_definition()
        {
            return ros::message_traits::Definition<T>::value();
        }

        template <typename T>
        std::string ros1_message_md5()
        {
            return ros::message_traits::MD5Sum<T>::value();
        }
    }  // namespace detail

    template <typename T>
    class Ros1Codec final : public ICodec<T>
    {
    public:
        SerializedPayload serialize(const T& value) override
        {
            const auto size = ros::serialization::serializationLength(value);

            SerializedPayload out(size);
            ros::serialization::OStream stream(
                reinterpret_cast<std::uint8_t*>(out.data()),
                static_cast<std::uint32_t>(out.size()));
            ros::serialization::serialize(stream, value);
            return out;
        }
 
        SchemaInfo schema() const override
        {
            return {
                detail::ros1_message_type<T>(),
                "ros1msg",
                detail::ros1_schema_data(detail::raw_ros1_message_definition<T>()),
            };
        }

        std::string_view message_encoding() const noexcept override
        {
            return "ros1";
        }
    };
#endif

}  // namespace flightLogger
