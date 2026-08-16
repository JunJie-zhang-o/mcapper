#pragma once

#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "codec.hpp"

namespace flightLogger
{
    template <typename T>
    class StructCodec final : public ICodec<T>
    {
        static_assert(std::is_trivially_copyable_v<T>, "StructCodec requires a trivially copyable message type");

    public:
        explicit StructCodec(std::string schema_name,
                             std::string schema_encoding = "struct",
                             SerializedPayload schema_data = {},
                             std::string message_encoding = "struct")
            : schema_{std::move(schema_name), std::move(schema_encoding), std::move(schema_data)},
              message_encoding_(std::move(message_encoding))
        {
            if (this->schema_.name.empty()) throw std::invalid_argument("struct schema name is empty");
            if (this->message_encoding_.empty()) throw std::invalid_argument("struct message encoding is empty");
        }

        SerializedPayload serialize(const T& value) override
        {
            SerializedPayload out(sizeof(T));
            std::memcpy(out.data(), &value, sizeof(T));
            return out;
        }

        SchemaInfo schema() const override
        {
            return this->schema_;
        }

        std::string_view message_encoding() const noexcept override
        {
            return this->message_encoding_;
        }

    private:
        SchemaInfo  schema_;
        std::string message_encoding_;
    };

}  // namespace flightLogger
