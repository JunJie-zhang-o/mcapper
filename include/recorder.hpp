#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ringbuffer.hpp"

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

    struct ChannelInfo
    {
        uint32_t                                     id{0};
        std::string                                  topic;
        std::string                                  message_encoding;
        std::string                                  schema_name;
        std::string                                  schema_encoding;
        std::vector<std::byte>                       schema_data;
        std::unordered_map<std::string, std::string> metadata;
    };

    struct FlightRecorderOptions
    {
        uint64_t                                     pre_trigger_ns{0};
        uint64_t                                     post_trigger_ns{0};
        std::string                                  profile{"flight_logger"};
        std::string                                  library{"flight_logger"};
        std::unordered_map<std::string, std::string> mcap_metadata;
    };

    class IFlightChannel
    {
    public:
        virtual ~IFlightChannel() = default;

        virtual uint32_t           id() const noexcept            = 0;
        virtual const ChannelInfo& info() const noexcept          = 0;
        virtual std::size_t        ring_capacity() const noexcept = 0;

        virtual void request_freeze() noexcept                              = 0;
        virtual bool acquire_frozen(uint64_t begin_time, uint64_t end_time) = 0;
        virtual void release_frozen() noexcept                              = 0;

        virtual bool                   has_current() const noexcept       = 0;
        virtual uint64_t               current_timestamp() const noexcept = 0;
        virtual std::vector<std::byte> serialize_current() const          = 0;
        virtual void                   advance() noexcept                 = 0;
    };

    template <typename T, std::size_t N>
    class FlightChannel final : public IFlightChannel
    {
    public:
        using Record     = TimedRecord<T>;
        using DoubleRing = DoubleRingBuffer<Record, N>;
        using Ring       = typename DoubleRing::Ring;
        using Serializer = std::function<std::vector<std::byte>(const T&)>;

        FlightChannel(ChannelInfo info, DoubleRing& ring, Serializer serializer) : info_(std::move(info)), ring_(ring), serializer_(std::move(serializer))
        {
            if (!this->serializer_) throw std::invalid_argument("flight channel serializer is empty");
        }

        uint32_t id() const noexcept override
        {
            return this->info_.id;
        }

        const ChannelInfo& info() const noexcept override
        {
            return this->info_;
        }

        std::size_t ring_capacity() const noexcept override
        {
            return N;
        }

        void request_freeze() noexcept override
        {
            this->ring_.request_freeze();
        }

        bool acquire_frozen(uint64_t begin_time, uint64_t end_time) override
        {
            std::size_t index = 0;
            const Ring* ring  = nullptr;

            if (!this->ring_.try_acquire_frozen(index, ring)) return false;

            this->frozen_index_ = index;
            this->frozen_ring_  = ring;
            this->records_.clear();
            this->cursor_ = 0;

            const auto append_in_window = [this, begin_time, end_time](std::span<const Record> records)
            {
                for (const auto& record : records)
                {
                    if (record.timestamp_ns >= begin_time && record.timestamp_ns <= end_time)
                    {
                        this->records_.push_back(&record);
                    }
                }
            };

            const auto segments = this->frozen_ring_->chronological_segments();
            append_in_window(segments.first);
            append_in_window(segments.second);

            return true;
        }

        void release_frozen() noexcept override
        {
            if (!this->frozen_ring_) return;

            this->records_.clear();
            this->cursor_      = 0;
            this->frozen_ring_ = nullptr;
            this->ring_.release_frozen(this->frozen_index_);
        }

        bool has_current() const noexcept override
        {
            return this->cursor_ < this->records_.size();
        }

        uint64_t current_timestamp() const noexcept override
        {
            return this->records_[this->cursor_]->timestamp_ns;
        }

        std::vector<std::byte> serialize_current() const override
        {
            return this->serializer_(this->records_[this->cursor_]->data);
        }

        void advance() noexcept override
        {
            if (this->cursor_ < this->records_.size()) ++this->cursor_;
        }

    private:
        ChannelInfo                info_;
        DoubleRing&                ring_;
        Serializer                 serializer_;
        std::size_t                frozen_index_{0};
        const Ring*                frozen_ring_{nullptr};
        std::vector<const Record*> records_;
        std::size_t                cursor_{0};
    };

    class FlightRecorder
    {
    public:
        explicit FlightRecorder(FlightRecorderOptions options);
        ~FlightRecorder();

        FlightRecorder(const FlightRecorder&)            = delete;
        FlightRecorder& operator=(const FlightRecorder&) = delete;

        template <typename T, std::size_t N>
        void register_channel(ChannelInfo info, DoubleRingBuffer<TimedRecord<T>, N>& ring, typename FlightChannel<T, N>::Serializer serializer)
        {
            info.id = this->allocate_channel_id();
            this->register_channel_erased(std::make_unique<FlightChannel<T, N>>(std::move(info), ring, std::move(serializer)));
        }

        template <typename T, std::size_t N>
        void register_channel(std::string topic, DoubleRingBuffer<TimedRecord<T>, N>& ring, MessageEncoding encoding);

        void start();

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
