#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace flightLogger
{

    template <typename T>
    struct TimedRecord
    {
        /// @brief Record timestamp in nanoseconds.
        uint64_t timestamp_ns;
        /// @brief Payload captured at the timestamp.
        T data;
    };

    template <typename T>
    class ConstSegment
    {
    public:
        using const_iterator = const T*;

        constexpr ConstSegment() noexcept = default;

        constexpr ConstSegment(const T* data, std::size_t size) noexcept : data_(data), size_(size)
        {
        }

        [[nodiscard]] constexpr const T* data() const noexcept
        {
            return this->data_;
        }

        [[nodiscard]] constexpr std::size_t size() const noexcept
        {
            return this->size_;
        }

        [[nodiscard]] constexpr bool empty() const noexcept
        {
            return this->size_ == 0;
        }

        [[nodiscard]] constexpr const_iterator begin() const noexcept
        {
            return this->data_;
        }

        [[nodiscard]] constexpr const_iterator end() const noexcept
        {
            return this->data_ == nullptr ? nullptr : this->data_ + this->size_;
        }

    private:
        const T*    data_{nullptr};
        std::size_t size_{0};
    };

    namespace detail
    {
        template <typename T, typename = void>
        struct HasTimestampNs : std::false_type
        {
        };

        template <typename T>
        struct HasTimestampNs<T, std::void_t<decltype(static_cast<uint64_t>(std::declval<const T&>().timestamp_ns))>>
            : std::true_type
        {
        };
    }  // namespace detail

    template <typename T>
    class OverwriteRingBuffer
    {
    public:
        /// @brief Construct a ring buffer with runtime capacity.
        explicit OverwriteRingBuffer(std::size_t capacity) : data_(capacity)
        {
            if (capacity == 0) [[unlikely]]
                throw std::invalid_argument("ring buffer capacity must be greater than zero");
        }

        struct Segments
        {
            /// @brief First contiguous chronological segment.
            ConstSegment<T> first;
            /// @brief Second wrapped chronological segment.
            ConstSegment<T> second;
        };

        /// @brief Append a copy of a value, overwriting the oldest entry when full.
        /// @param value Value to append.
        void push(const T& value)
        {
            this->data_[this->write_] = value;
            this->advance();
        }

        /// @brief Append a moved value, overwriting the oldest entry when full.
        /// @param value Value to append.
        void push(T&& value)
        {
            this->data_[this->write_] = std::move(value);
            this->advance();
        }

        /// @brief Remove all stored elements and reset write position.
        void clear() noexcept
        {
            this->write_ = 0;
            this->size_  = 0;
        }

        /// @brief Get the number of valid elements currently stored.
        /// @return Current element count.
        [[nodiscard]] std::size_t size() const noexcept
        {
            return this->size_;
        }

        /// @brief Get the fixed storage capacity.
        /// @return Maximum number of elements the buffer can hold.
        [[nodiscard]] std::size_t capacity() const noexcept
        {
            return this->data_.size();
        }

        /// @brief Check whether the buffer contains no elements.
        /// @return True when empty.
        [[nodiscard]] bool empty() const noexcept
        {
            return this->size_ == 0;
        }

        /// @brief Check whether the buffer has reached full capacity.
        /// @return True when full.
        [[nodiscard]] bool full() const noexcept
        {
            return this->size_ == this->data_.size();
        }

        /// @brief Access stored elements in chronological order as up to two segments.
        /// @return One or two contiguous segments covering all valid elements.
        [[nodiscard]] Segments chronological_segments() const noexcept
        {
            if (this->size_ == 0) return {};

            if (this->size_ < this->data_.size())
            {
                return {ConstSegment<T>{this->data_.data(), this->size_}, {}};
            }

            return {ConstSegment<T>{this->data_.data() + this->write_, this->data_.size() - this->write_},
                    ConstSegment<T>{this->data_.data(), this->write_}};
        }

    private:
        /// @brief Advance the write cursor and grow size until capacity is reached.
        void advance() noexcept
        {
            ++this->write_;

            if (this->write_ == this->data_.size()) this->write_ = 0;

            if (this->size_ < this->data_.size()) ++this->size_;
        }

    private:
        std::vector<T> data_;
        std::size_t    write_{0};
        std::size_t    size_{0};
    };

    template <typename T>
    class BlackBox
    {
    public:
        using Ring = OverwriteRingBuffer<T>;

        BlackBox(std::size_t pre_capacity, std::size_t post_capacity)
            : buffers_{Ring{pre_capacity}, Ring{post_capacity}, Ring{pre_capacity}}
        {
        }

        /// @brief Append a copied value to the active ring buffer.
        /// @param value Value to append.
        void push(const T& value)
        {
            std::lock_guard<std::mutex> lock(this->mutex_);
            if (this->active_ == kPostIndex && this->buffers_[this->active_].full()) return;

            this->remember_first_record_time_locked(value);
            this->buffers_[this->active_].push(value);
        }

        /// @brief Append a moved value to the active ring buffer.
        /// @param value Value to append.
        void push(T&& value)
        {
            std::lock_guard<std::mutex> lock(this->mutex_);
            if (this->active_ == kPostIndex && this->buffers_[this->active_].full()) return;

            this->remember_first_record_time_locked(value);
            this->buffers_[this->active_].push(std::move(value));
        }

        /// @brief Freeze the current pre-trigger buffer and switch active writes to the post-trigger buffer.
        void request_freeze_pre() noexcept
        {
            std::lock_guard<std::mutex> lock(this->mutex_);
            if (this->frozen_pre_index_ != kNoFrozen) return;

            this->frozen_pre_index_ = static_cast<int>(this->active_);
            this->buffers_[kPostIndex].clear();
            this->active_ = kPostIndex;
        }

        /// @brief Freeze the post-trigger buffer and switch active writes to an available pre-trigger buffer.
        void request_freeze_post() noexcept
        {
            std::lock_guard<std::mutex> lock(this->mutex_);
            if (this->frozen_post_index_ != kNoFrozen) return;

            this->frozen_post_index_ = static_cast<int>(kPostIndex);
            this->active_            = this->next_pre_active_locked();
            this->buffers_[this->active_].clear();
        }

        /// @brief Try to acquire the frozen ring for exclusive dumping.
        /// @param index Receives the frozen buffer index on success.
        /// @param ring Receives the frozen buffer pointer on success.
        /// @return True if a frozen buffer was acquired.
        bool try_acquire_frozen_pre(std::size_t& index, const Ring*& ring) noexcept
        {
            return this->try_acquire_frozen(this->frozen_pre_index_, this->pre_dumping_, index, ring);
        }

        /// @brief Try to acquire the post-trigger frozen ring for exclusive dumping.
        /// @param index Receives the frozen buffer index on success.
        /// @param ring Receives the frozen buffer pointer on success.
        /// @return True if a frozen buffer was acquired.
        bool try_acquire_frozen_post(std::size_t& index, const Ring*& ring) noexcept
        {
            return this->try_acquire_frozen(this->frozen_post_index_, this->post_dumping_, index, ring);
        }

        /// @brief Release a previously acquired pre-trigger frozen ring and mark it reusable.
        /// @param index Index returned by `try_acquire_frozen_pre`.
        void release_frozen_pre(std::size_t index)
        {
            this->release_frozen(this->frozen_pre_index_, this->pre_dumping_, index);
        }

        /// @brief Release a previously acquired post-trigger frozen ring and mark it reusable.
        /// @param index Index returned by `try_acquire_frozen_post`.
        void release_frozen_post(std::size_t index)
        {
            this->release_frozen(this->frozen_post_index_, this->post_dumping_, index);
        }

        [[nodiscard]] std::size_t pre_capacity() const noexcept
        {
            return this->buffers_[kPreAIndex].capacity();
        }

        [[nodiscard]] std::size_t post_capacity() const noexcept
        {
            return this->buffers_[kPostIndex].capacity();
        }

        [[nodiscard]] bool post_full() const noexcept
        {
            std::lock_guard<std::mutex> lock(this->mutex_);
            return this->buffers_[kPostIndex].full();
        }

        [[nodiscard]] std::optional<uint64_t> first_record_time_ns() const noexcept
        {
            std::lock_guard<std::mutex> lock(this->mutex_);
            return this->first_record_time_ns_;
        }

    private:
        static constexpr std::size_t kPreAIndex = 0;
        static constexpr std::size_t kPostIndex = 1;
        static constexpr std::size_t kPreCIndex = 2;
        static constexpr int         kNoFrozen  = -1;

        void remember_first_record_time_locked(const T& value)
        {
            if (this->first_record_time_ns_.has_value()) return;

            if constexpr (detail::HasTimestampNs<T>::value)
            {
                this->first_record_time_ns_ = static_cast<uint64_t>(value.timestamp_ns);
            }
        }

        bool try_acquire_frozen(int frozen_index, bool& dumping, std::size_t& index, const Ring*& ring) noexcept
        {
            std::lock_guard<std::mutex> lock(this->mutex_);
            if (frozen_index == kNoFrozen || dumping) return false;

            dumping = true;
            index   = static_cast<std::size_t>(frozen_index);
            ring    = &this->buffers_[index];
            return true;
        }

        void release_frozen(int& frozen_index, bool& dumping, std::size_t index)
        {
            std::lock_guard<std::mutex> lock(this->mutex_);
            if (frozen_index != static_cast<int>(index)) return;

            this->buffers_[index].clear();
            frozen_index = kNoFrozen;
            dumping      = false;
        }

        std::size_t next_pre_active_locked() const noexcept
        {
            if (this->frozen_pre_index_ != static_cast<int>(kPreAIndex)) return kPreAIndex;

            return kPreCIndex;
        }

    private:
        std::array<Ring, 3> buffers_;
        std::size_t         active_{kPreAIndex};
        int                 frozen_pre_index_{kNoFrozen};
        int                 frozen_post_index_{kNoFrozen};
        bool                pre_dumping_{false};
        bool                post_dumping_{false};
        std::optional<uint64_t> first_record_time_ns_;
        mutable std::mutex  mutex_;
    };

}  // namespace flightLogger
