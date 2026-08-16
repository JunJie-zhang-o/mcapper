#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sstream>
#include <utility>

#ifndef FLIGHTLOGGER_ENABLE_ROS2
#define FLIGHTLOGGER_ENABLE_ROS2 0
#endif

#if FLIGHTLOGGER_ENABLE_ROS2
#include <ament_index_cpp/get_package_share_directory.hpp>
#include <rclcpp/serialization.hpp>
#include <rosidl_runtime_cpp/traits.hpp>
#endif

#include "codec.hpp"

namespace flightLogger
{
#if FLIGHTLOGGER_ENABLE_ROS2
    namespace detail
    {
        struct Ros2MessageType
        {
            std::string package_name;
            std::string interface_folder;
            std::string message_name;
        };

        inline SerializedPayload ros2_schema_data(std::string_view value)
        {
            if (value.empty()) return {};

            const auto* begin = reinterpret_cast<const std::byte*>(value.data());
            return {begin, begin + value.size()};
        }

        inline SerializedPayload ros2_serialized_payload(const std::uint8_t* data, std::size_t size)
        {
            if (size == 0) return {};

            const auto* begin = reinterpret_cast<const std::byte*>(data);
            return {begin, begin + size};
        }

        inline Ros2MessageType parse_ros2_message_type(std::string_view type_name)
        {
            const auto first_slash = type_name.find('/');
            const auto last_slash  = type_name.rfind('/');

            if (first_slash == std::string_view::npos || last_slash == std::string_view::npos || first_slash == last_slash)
            {
                throw std::invalid_argument("ROS2 message type must look like pkg/msg/Name: " + std::string(type_name));
            }

            return {
                std::string(type_name.substr(0, first_slash)),
                std::string(type_name.substr(first_slash + 1, last_slash - first_slash - 1)),
                std::string(type_name.substr(last_slash + 1)),
            };
        }

        inline std::string read_ros2_text_file(const std::string& path)
        {
            std::ifstream file(path);
            if (!file) throw std::runtime_error("failed to open ROS2 message definition: " + path);

            std::ostringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }

        inline std::string raw_ros2_message_definition_from_type_name(std::string_view type_name)
        {
            const auto parsed            = parse_ros2_message_type(type_name);
            const auto package_share_dir = ament_index_cpp::get_package_share_directory(parsed.package_name);
            const auto definition_path   = package_share_dir + "/" + parsed.interface_folder + "/" + parsed.message_name + ".msg";
            return read_ros2_text_file(definition_path);
        }

        template <typename T>
        std::string raw_ros2_message_definition()
        {
            return raw_ros2_message_definition_from_type_name(rosidl_generator_traits::name<T>());
        }
    }  // namespace detail

    template <typename T>
    class Ros2Codec final : public ICodec<T>
    {
    public:
        Ros2Codec() : Ros2Codec(detail::raw_ros2_message_definition<T>())
        {
        }

        explicit Ros2Codec(std::string_view schema_definition,
                           std::string schema_encoding = "ros2msg",
                           std::string schema_name     = rosidl_generator_traits::name<T>())
            : Ros2Codec(detail::ros2_schema_data(schema_definition), std::move(schema_encoding), std::move(schema_name))
        {
        }

        explicit Ros2Codec(SerializedPayload schema_data,
                           std::string schema_encoding = "ros2msg",
                           std::string schema_name     = rosidl_generator_traits::name<T>())
            : schema_{std::move(schema_name), std::move(schema_encoding), std::move(schema_data)}
        {
            if (this->schema_.name.empty()) throw std::invalid_argument("ros2 schema name is empty");
            if (this->schema_.encoding.empty()) throw std::invalid_argument("ros2 schema encoding is empty");
        }

        SerializedPayload serialize(const T& value) override
        {
            rclcpp::SerializedMessage serialized;
            this->serialization_.serialize_message(&value, &serialized);

            const auto& raw = serialized.get_rcl_serialized_message();
            return detail::ros2_serialized_payload(raw.buffer, raw.buffer_length);
        }

        SchemaInfo schema() const override
        {
            return this->schema_;
        }

        std::string_view message_encoding() const noexcept override
        {
            return "cdr";
        }

    private:
        SchemaInfo               schema_;
        rclcpp::Serialization<T> serialization_;
    };
#endif

}  // namespace flightLogger
