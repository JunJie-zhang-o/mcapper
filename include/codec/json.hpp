#pragma once

#include <rfl/json.hpp>

#include "codec.hpp"

#include <string>
#include <string_view>

namespace flightLogger
{
    namespace detail
    {
        template <typename T>
        std::string type_name();

        inline SerializedPayload bytes_from_string(std::string_view value);
    }  // namespace detail

    template <typename T>
    class JsonCodec final : public ICodec<T>
    {
    public:
        SerializedPayload serialize(const T& data) override
        {
            this->buffer_ = rfl::json::write(data);
            return detail::bytes_from_string(this->buffer_);
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
            return "json";
        }

    private:
        std::string buffer_;
    };

}  // namespace flightLogger
