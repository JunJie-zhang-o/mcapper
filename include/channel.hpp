#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ringbuffer.hpp"
#include "serializer.hpp"

namespace flightLogger
{

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

    class IFlightChannel
    {
    public:
        virtual ~IFlightChannel() = default;

        virtual uint32_t           id() const noexcept            = 0;
        virtual const ChannelInfo& info() const noexcept          = 0;
        virtual std::size_t        ring_capacity() const noexcept = 0;

        virtual void request_freeze_pre() noexcept                                      = 0;
        virtual void request_freeze_post() noexcept                                     = 0;
        virtual bool acquire_frozen_pre(uint64_t begin_time, uint64_t end_time)         = 0;
        virtual bool acquire_frozen_post(uint64_t begin_time, uint64_t end_time)        = 0;
        virtual void release_frozen_pre() noexcept                                      = 0;
        virtual void release_frozen_post() noexcept                                     = 0;

        virtual bool              has_current() const noexcept       = 0;
        virtual uint64_t          current_timestamp() const noexcept = 0;
        virtual SerializedPayload serialize_current() const          = 0;
        virtual void              advance() noexcept                 = 0;
    };

    template <typename T, std::size_t N>
    class FlightChannel final : public IFlightChannel
    {
    private:
        enum class FreezeKind
        {
            None,
            Pre,
            Post,
        };

    public:
        using Record     = TimedRecord<T>;
        using TripleRing = TripleRingBuffer<Record, N>;
        using Ring       = typename TripleRing::Ring;
        using Serializer = std::unique_ptr<ISerializer<T>>;

        FlightChannel(ChannelInfo info, TripleRing& ring, Serializer serializer) : info_(std::move(info)), ring_(ring), serializer_(std::move(serializer))
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

        void request_freeze_pre() noexcept override
        {
            this->ring_.request_freeze_pre();
        }

        void request_freeze_post() noexcept override
        {
            this->ring_.request_freeze_post();
        }

        bool acquire_frozen_pre(uint64_t begin_time, uint64_t end_time) override
        {
            return this->acquire_frozen(begin_time, end_time, FreezeKind::Pre);
        }

        bool acquire_frozen_post(uint64_t begin_time, uint64_t end_time) override
        {
            return this->acquire_frozen(begin_time, end_time, FreezeKind::Post);
        }

        void release_frozen_pre() noexcept override
        {
            this->release_frozen(FreezeKind::Pre);
        }

        void release_frozen_post() noexcept override
        {
            this->release_frozen(FreezeKind::Post);
        }

        bool has_current() const noexcept override
        {
            return this->cursor_ < this->records_.size();
        }

        uint64_t current_timestamp() const noexcept override
        {
            return this->records_[this->cursor_]->timestamp_ns;
        }

        SerializedPayload serialize_current() const override
        {
            return this->serializer_->serialize(this->records_[this->cursor_]->data);
        }

        void advance() noexcept override
        {
            if (this->cursor_ < this->records_.size()) ++this->cursor_;
        }

    private:
        bool acquire_frozen(uint64_t begin_time, uint64_t end_time, FreezeKind kind)
        {
            std::size_t index = 0;
            const Ring* ring  = nullptr;

            const bool acquired =
                kind == FreezeKind::Pre
                    ? this->ring_.try_acquire_frozen_pre(index, ring)
                    : this->ring_.try_acquire_frozen_post(index, ring);

            if (!acquired) return false;

            this->frozen_index_ = index;
            this->frozen_ring_  = ring;
            this->freeze_kind_  = kind;
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

        void release_frozen(FreezeKind kind) noexcept
        {
            if (!this->frozen_ring_ || this->freeze_kind_ != kind) return;

            this->records_.clear();
            this->cursor_      = 0;
            this->frozen_ring_ = nullptr;
            this->freeze_kind_ = FreezeKind::None;

            if (kind == FreezeKind::Pre)
                this->ring_.release_frozen_pre(this->frozen_index_);
            else
                this->ring_.release_frozen_post(this->frozen_index_);
        }

        ChannelInfo                info_;
        TripleRing&                ring_;
        Serializer                 serializer_;
        std::size_t                frozen_index_{0};
        const Ring*                frozen_ring_{nullptr};
        FreezeKind                 freeze_kind_{FreezeKind::None};
        std::vector<const Record*> records_;
        std::size_t                cursor_{0};
    };

}  // namespace flightLogger
