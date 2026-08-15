#pragma once

#include <rfl/enums.hpp>
#include <rfl/json.hpp>
#include <rfl/type_name_t.hpp>

#include "codec.hpp"
#include "recorder.hpp"

#ifndef FLIGHTLOGGER_ENABLE_MSGPACK
#define FLIGHTLOGGER_ENABLE_MSGPACK 0
#endif

#if FLIGHTLOGGER_ENABLE_MSGPACK
#include <rfl/msgpack.hpp>
#endif

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
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
            // 直接复用 reflect-cpp 的编译期类型名(GCC / Clang / MSVC 均支持)。
            // rfl::type_name_t<T> 是 rfl::Literal<...> 的别名,.str() 转为 std::string。
            return rfl::type_name_t<T>().str();
        }

        inline std::string unsupported_encoding_message(MessageEncoding encoding)
        {
            return "flightLogger cannot auto-generate serializer for encoding '" + rfl::enum_to_string(encoding) + "'";
        }
    }  // namespace detail

    template <typename T>
    class JsonCodec final : public ICodec<T>
    {
    public:
        SerializedPayload serialize(const T& data) override
        {
            this->buffer_ = rfl::json::write(data);
            return detail::to_bytes(this->buffer_);
        }

        SchemaInfo schema() const override
        {
            return {
                detail::type_name<T>(),
                "jsonschema",
                detail::to_bytes(rfl::json::to_schema<T>()),
            };
        }

        MessageEncoding message_encoding() const noexcept override
        {
            return MessageEncoding::Json;
        }

    private:
        std::string buffer_;
    };

    template <typename T>
    class MsgPackCodec final : public ICodec<T>
    {
    public:
        SerializedPayload serialize(const T& data) override
        {
#if FLIGHTLOGGER_ENABLE_MSGPACK
            this->buffer_ = rfl::msgpack::write(data);
            return detail::to_bytes(this->buffer_);
#else
            (void)data;
            throw std::invalid_argument(
                "flightLogger MsgPack support requires FLIGHTLOGGER_ENABLE_MSGPACK=1 "
                "and reflect-cpp MsgPack dependencies");
#endif
        }

        SchemaInfo schema() const override
        {
            return {
                detail::type_name<T>(),
                "jsonschema",
                detail::to_bytes(rfl::json::to_schema<T>()),
            };
        }

        MessageEncoding message_encoding() const noexcept override
        {
            return MessageEncoding::MsgPack;
        }

    private:
        std::vector<char> buffer_;
    };

    namespace detail
    {

        template <typename T>
        std::unique_ptr<ICodec<T>> make_codec(MessageEncoding encoding)
        {
            switch (encoding)
            {
                case MessageEncoding::Json:
                    return std::make_unique<JsonCodec<T>>();

                case MessageEncoding::MsgPack:
#if FLIGHTLOGGER_ENABLE_MSGPACK
                    return std::make_unique<MsgPackCodec<T>>();
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

}  // namespace flightLogger
