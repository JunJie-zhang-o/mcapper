#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>

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

    template <typename T, std::size_t N>
    class OverwriteRingBuffer
    {
    public:
        /// @brief Construct a ring buffer with compile-time capacity.
        OverwriteRingBuffer()
        {
            if (N == 0) [[unlikely]]
                throw std::invalid_argument("ring buffer capacity must be greater than zero");
        }

        struct Segments
        {
            /// @brief First contiguous chronological segment.
            std::span<const T> first;
            /// @brief Second wrapped chronological segment.
            std::span<const T> second;
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
        [[nodiscard]] static constexpr std::size_t capacity() noexcept
        {
            return N;
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
            return this->size_ == N;
        }

        /// @brief Access stored elements in chronological order as up to two spans.
        /// @return One or two contiguous spans covering all valid elements.
        [[nodiscard]] Segments chronological_segments() const noexcept
        {
            if (this->size_ == 0) return {};

            if (this->size_ < N)
            {
                return {std::span<const T>{this->data_.data(), this->size_}, {}};
            }

            return {std::span<const T>{this->data_.data() + this->write_, N - this->write_}, std::span<const T>{this->data_.data(), this->write_}};
        }

    private:
        /// @brief Advance the write cursor and grow size until capacity is reached.
        void advance() noexcept
        {
            ++this->write_;

            if (this->write_ == N) this->write_ = 0;

            if (this->size_ < N) ++this->size_;
        }

    private:
        std::array<T, N> data_{};
        std::size_t      write_{0};
        std::size_t      size_{0};
    };

    template <typename T, std::size_t N>
    class TripleRingBuffer
    {
    public:
        /// @brief Special values used by the frozen-state atomic.
        enum class FrozenState : int
        {
            /// @brief 正在转储冻结缓冲区，当前不可复用。
            Dumping = -2,
            /// @brief 当前没有已冻结的缓冲区。
            NoFrozen = -1,
        };

        using Ring = OverwriteRingBuffer<T, N>;

        /// @brief Append a copied value to the active ring buffer.
        /// @param value Value to append.
        void push(const T& value)
        {
            this->service_freeze_requests();
            this->buffers_[this->active_].push(value);
        }

        /// @brief Append a moved value to the active ring buffer.
        /// @param value Value to append.
        void push(T&& value)
        {
            this->service_freeze_requests();
            this->buffers_[this->active_].push(std::move(value));
        }

        /// @brief Request that the current active buffer be frozen as pre-trigger data on the next push.
        void request_freeze_pre() noexcept
        {
            this->freeze_pre_requested_.store(true, std::memory_order_release);
        }

        /// @brief Request that the current active buffer be frozen as post-trigger data on the next push.
        void request_freeze_post() noexcept
        {
            this->freeze_post_requested_.store(true, std::memory_order_release);
        }

        /// @brief Try to acquire the frozen ring for exclusive dumping.
        /// @param index Receives the frozen buffer index on success.
        /// @param ring Receives the frozen buffer pointer on success.
        /// @return True if a frozen buffer was acquired.
        bool try_acquire_frozen_pre(std::size_t& index, const Ring*& ring) noexcept
        {
            return this->try_acquire_frozen(this->frozen_pre_state_, this->frozen_pre_index_, index, ring);
        }

        /// @brief Try to acquire the post-trigger frozen ring for exclusive dumping.
        /// @param index Receives the frozen buffer index on success.
        /// @param ring Receives the frozen buffer pointer on success.
        /// @return True if a frozen buffer was acquired.
        bool try_acquire_frozen_post(std::size_t& index, const Ring*& ring) noexcept
        {
            return this->try_acquire_frozen(this->frozen_post_state_, this->frozen_post_index_, index, ring);
        }

        /// @brief Release a previously acquired pre-trigger frozen ring and mark it reusable.
        /// @param index Index returned by `try_acquire_frozen_pre`.
        void release_frozen_pre(std::size_t index)
        {
            this->release_frozen(this->frozen_pre_state_, this->frozen_pre_index_, index);
        }

        /// @brief Release a previously acquired post-trigger frozen ring and mark it reusable.
        /// @param index Index returned by `try_acquire_frozen_post`.
        void release_frozen_post(std::size_t index)
        {
            this->release_frozen(this->frozen_post_state_, this->frozen_post_index_, index);
        }

    private:
        bool try_acquire_frozen(std::atomic<int>& state, std::atomic<int>& frozen_index, std::size_t& index, const Ring*& ring) noexcept
        {
            int expected = state.load(std::memory_order_acquire);

            if (expected < 0) return false;

            if (!state.compare_exchange_strong(expected, static_cast<int>(FrozenState::Dumping), std::memory_order_acq_rel, std::memory_order_acquire))
                return false;

            index = static_cast<std::size_t>(expected);
            frozen_index.store(expected, std::memory_order_release);
            ring = &this->buffers_[index];
            return true;
        }

        void release_frozen(std::atomic<int>& state, std::atomic<int>& frozen_index, std::size_t index)
        {
            this->buffers_[index].clear();
            state.store(static_cast<int>(FrozenState::NoFrozen), std::memory_order_release);
            frozen_index.store(static_cast<int>(FrozenState::NoFrozen), std::memory_order_release);
        }

        /// @brief Switch active buffers when a freeze request is pending.
        void service_freeze_requests()
        {
            if (this->freeze_pre_requested_.exchange(false, std::memory_order_acq_rel))
            {
                if (!this->freeze_active_into(this->frozen_pre_state_, this->frozen_pre_index_))
                    this->freeze_pre_requested_.store(true, std::memory_order_release);
            }

            if (this->freeze_post_requested_.exchange(false, std::memory_order_acq_rel))
            {
                if (!this->freeze_active_into(this->frozen_post_state_, this->frozen_post_index_))
                    this->freeze_post_requested_.store(true, std::memory_order_release);
            }
        }

        bool freeze_active_into(std::atomic<int>& state, std::atomic<int>& frozen_index)
        {
            if (state.load(std::memory_order_acquire) != static_cast<int>(FrozenState::NoFrozen)) return false;

            std::size_t next_active = 0;
            if (!this->find_next_active(next_active)) return false;

            const std::size_t old_active = this->active_;
            this->active_                = next_active;

            frozen_index.store(static_cast<int>(old_active), std::memory_order_release);
            state.store(static_cast<int>(old_active), std::memory_order_release);
            return true;
        }

        bool find_next_active(std::size_t& next_active) const noexcept
        {
            for (std::size_t offset = 1; offset < 3; ++offset)
            {
                const std::size_t candidate = (this->active_ + offset) % 3;

                if (this->is_available(candidate))
                {
                    next_active = candidate;
                    return true;
                }
            }

            return false;
        }

        bool is_available(std::size_t index) const noexcept
        {
            return !this->is_frozen_index(this->frozen_pre_state_, this->frozen_pre_index_, index) &&
                   !this->is_frozen_index(this->frozen_post_state_, this->frozen_post_index_, index);
        }

        bool is_frozen_index(const std::atomic<int>& state, const std::atomic<int>& frozen_index, std::size_t index) const noexcept
        {
            if (state.load(std::memory_order_acquire) == static_cast<int>(FrozenState::NoFrozen)) return false;

            return frozen_index.load(std::memory_order_acquire) == static_cast<int>(index);
        }

    private:
        Ring              buffers_[3];
        std::size_t       active_{0};
        std::atomic<int>  frozen_pre_state_{static_cast<int>(FrozenState::NoFrozen)};
        std::atomic<int>  frozen_pre_index_{static_cast<int>(FrozenState::NoFrozen)};
        std::atomic<int>  frozen_post_state_{static_cast<int>(FrozenState::NoFrozen)};
        std::atomic<int>  frozen_post_index_{static_cast<int>(FrozenState::NoFrozen)};
        std::atomic<bool> freeze_pre_requested_{false};
        std::atomic<bool> freeze_post_requested_{false};
    };

}  // namespace flightLogger
