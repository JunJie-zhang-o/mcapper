#pragma once

#include <rfl/enums.hpp>
#include <rfl/type_name_t.hpp>

#include "codec.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace flightLogger::detail
{
    inline SerializedPayload bytes_from_string(std::string_view value)
    {
        if (value.empty()) return {};

        const auto* begin = reinterpret_cast<const std::byte*>(value.data());
        return {begin, begin + value.size()};
    }

    inline SerializedPayload bytes_from_uint8(const std::uint8_t* data, std::size_t size)
    {
        if (size == 0) return {};

        const auto* begin = reinterpret_cast<const std::byte*>(data);
        return {begin, begin + size};
    }

    inline SerializedPayload bytes_from_chars(const std::vector<char>& value)
    {
        if (value.empty()) return {};

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
}  // namespace flightLogger::detail

#include "codec/json.hpp"
#include "codec/msgpack.hpp"

namespace flightLogger::detail
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
}  // namespace flightLogger::detail
