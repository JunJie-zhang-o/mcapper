#pragma once

#include <rfl/enums.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "channel.hpp"
#include "codec.hpp"

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

    namespace detail
    {
        template <typename T>
        std::unique_ptr<ICodec<T>> make_codec(MessageEncoding encoding);
    }  // namespace detail

    enum class RecorderState
    {
        Idle,
        Armed,
        FreezingPreTrigger,
        PostTrigger,
        FreezingPostTrigger,
        Dumping,
        Finalizing,
    };

    struct FlightRecorderOptions
    {
        uint64_t                                     pre_trigger_ns{0};
        uint64_t                                     post_trigger_ns{0};
        std::string                                  profile{"flight_logger"};
        std::string                                  library{"flight_logger"};
        std::unordered_map<std::string, std::string> mcap_metadata;
    };

    class FlightRecorder
    {
    public:
        explicit FlightRecorder(FlightRecorderOptions options);
        ~FlightRecorder();

        FlightRecorder(const FlightRecorder&)            = delete;
        FlightRecorder& operator=(const FlightRecorder&) = delete;

        template <typename T, std::size_t N>
        void register_channel(ChannelInfo info, TripleRingBuffer<TimedRecord<T>, N>& ring, typename FlightChannel<T, N>::Serializer serializer)
        {
            info.id = this->allocate_channel_id();
            this->register_channel_erased(std::make_unique<FlightChannel<T, N>>(std::move(info), ring, std::move(serializer)));
        }

        template <typename T, std::size_t N>
        void register_channel(ChannelInfo info, TripleRingBuffer<TimedRecord<T>, N>& ring, typename FunctionSerializer<T>::Function serializer)
        {
            this->register_channel<T, N>(
                std::move(info),
                ring,
                std::make_unique<FunctionSerializer<T>>(std::move(serializer)));
        }

        template <typename T, std::size_t N>
        void register_channel(std::string topic, TripleRingBuffer<TimedRecord<T>, N>& ring, MessageEncoding encoding)
        {
            using Value = std::remove_cvref_t<T>;

            ChannelInfo info;
            auto        codec  = detail::make_codec<Value>(encoding);
            auto        schema = codec->schema();

            info.topic             = std::move(topic);
            info.message_encoding  = rfl::enum_to_string(codec->message_encoding());
            info.schema_name       = std::move(schema.name);
            info.schema_encoding   = std::move(schema.encoding);
            info.schema_data       = std::move(schema.data);
            info.metadata["topic"] = info.topic;

            this->register_channel<Value, N>(std::move(info), ring, std::move(codec));
        }

        void start();

        RecorderState state() const noexcept;

        void trigger(uint64_t trigger_time_ns, std::filesystem::path output_path, std::string reason = {});

        void stop();

    private:
        class Impl;

        uint32_t allocate_channel_id();
        void     register_channel_erased(std::unique_ptr<IFlightChannel> channel);

    private:
        std::unique_ptr<Impl> impl_;
    };

}  // namespace flightLogger
