#pragma once

#include <rfl/enums.hpp>
#include <rfl/json.hpp>

#include "recorder.hpp"

#ifndef FLIGHTLOGGER_ENABLE_MSGPACK
#define FLIGHTLOGGER_ENABLE_MSGPACK 0
#endif

#if FLIGHTLOGGER_ENABLE_MSGPACK
#include <rfl/msgpack.hpp>
#endif

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace flightLogger
{
    namespace detail
    {

        inline std::vector<std::byte> to_bytes(std::string_view value)
        {
            const auto* begin = reinterpret_cast<const std::byte*>(value.data());
            return {begin, begin + value.size()};
        }

        inline std::vector<std::byte> to_bytes(const std::vector<char>& value)
        {
            const auto* begin = reinterpret_cast<const std::byte*>(value.data());
            return {begin, begin + value.size()};
        }

        template <typename T>
        std::string type_name()
        {
            constexpr std::string_view prefix = "std::string flightLogger::detail::type_name() [with T = ";
            constexpr std::string_view suffix = "; std::string = std::__cxx11::basic_string<char>]";
            std::string_view           name   = __PRETTY_FUNCTION__;

            if (name.starts_with(prefix) && name.ends_with(suffix))
            {
                name.remove_prefix(prefix.size());
                name.remove_suffix(suffix.size());
            }

            return std::string{name};
        }

        inline std::string unsupported_encoding_message(MessageEncoding encoding)
        {
            return "flightLogger cannot auto-generate serializer for encoding '" + rfl::enum_to_string(encoding) + "'";
        }

        template <typename T>
        std::function<std::vector<std::byte>(const T&)> make_rfl_serializer(MessageEncoding encoding)
        {
            switch (encoding)
            {
                case MessageEncoding::Json:
                    return [](const T& value) { return to_bytes(rfl::json::write(value)); };

                case MessageEncoding::MsgPack:
#if FLIGHTLOGGER_ENABLE_MSGPACK
                    return [](const T& value) { return to_bytes(rfl::msgpack::write(value)); };
#else
                    throw std::invalid_argument(
                        "flightLogger MsgPack support requires FLIGHTLOGGER_ENABLE_MSGPACK=1 "
                        "and reflect-cpp MsgPack dependencies");
#endif

                default:
                    throw std::invalid_argument(unsupported_encoding_message(encoding));
            }
        }

    }  // namespace detail

    template <typename T, std::size_t N>
    void FlightRecorder::register_channel(std::string topic, DoubleRingBuffer<TimedRecord<T>, N>& ring, MessageEncoding encoding)
    {
        using Value = std::remove_cvref_t<T>;

        ChannelInfo info;
        info.topic             = std::move(topic);
        info.message_encoding  = rfl::enum_to_string(encoding);
        info.schema_name       = detail::type_name<Value>();
        info.schema_encoding   = "jsonschema";
        info.schema_data       = detail::to_bytes(rfl::json::to_schema<Value>());
        info.metadata["topic"] = info.topic;

        this->register_channel<Value, N>(std::move(info), ring, detail::make_rfl_serializer<Value>(encoding));
    }

}  // namespace flightLogger
