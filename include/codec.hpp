#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "serializer.hpp"

namespace flightLogger
{

    enum class MessageEncoding
    {
        Ros1,
        Cdr,
        Protobuf,
        Flatbuffer,
        CapnProto,
        Cbor,
        MsgPack,
        Json,
    };

    enum class SchemaEncoding
    {
        None,
        Protobuf,
        Flatbuffer,
        CapnProto,
        Ros1Msg,
        Ros2Msg,
        Ros2Idl,
        OmgIdl,
        JsonSchema,
    };

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

        virtual std::string_view message_encoding() const noexcept = 0;
    };

    namespace detail
    {
        template <typename T>
        std::unique_ptr<ICodec<T>> make_codec(MessageEncoding encoding);
    }  // namespace detail

}  // namespace flightLogger
