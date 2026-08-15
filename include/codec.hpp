#pragma once

#include <string>

#include "serializer.hpp"

namespace flightLogger
{

    enum class MessageEncoding;

    struct SchemaInfo
    {
        std::string       name;
        std::string       encoding;
        SerializedPayload data;
    };

    template <typename T>
    class ICodec : public ISerializer<T>
    {
    public:
        virtual ~ICodec() = default;

        virtual SerializedPayload serialize(const T& value) override = 0;

        virtual SchemaInfo schema() const = 0;

        virtual MessageEncoding message_encoding() const noexcept = 0;
    };

}  // namespace flightLogger
