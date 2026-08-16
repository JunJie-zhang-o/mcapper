#pragma once

#include <rfl/enums.hpp>
#include <rfl/json.hpp>

#include "codec/json.hpp"

#ifndef FLIGHTLOGGER_ENABLE_MSGPACK
#define FLIGHTLOGGER_ENABLE_MSGPACK 0
#endif

#if FLIGHTLOGGER_ENABLE_MSGPACK
#include <rfl/msgpack.hpp>
#endif

#include <stdexcept>
#include <string_view>
#include <vector>

namespace flightLogger
{
    namespace detail
    {
        inline SerializedPayload bytes_from_chars(const std::vector<char>& value);
    }  // namespace detail 
 
    template <typename T>
    class MsgPackCodec final : public ICodec<T>
    {
    public:
        SerializedPayload serialize(const T& data) override
        {
#if FLIGHTLOGGER_ENABLE_MSGPACK
            this->buffer_ = rfl::msgpack::write(data);
            return detail::bytes_from_chars(this->buffer_);  
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
                detail::bytes_from_string(rfl::json::to_schema<T>()),
            };
        }

        std::string_view message_encoding() const noexcept override
        {
            return "msgpack";
        }

    private:
        std::vector<char> buffer_;
    };

}  // namespace flightLogger
