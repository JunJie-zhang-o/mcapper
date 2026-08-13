#pragma once

#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mcapper
{

    template <typename T>
    class RingBuffer
    {
    public:
        explicit RingBuffer(std::size_t capacity) : capacity_(capacity + 1), storage_(capacity + 1)
        {
            if (capacity == 0)
            {
                throw std::invalid_argument("ring buffer capacity must be greater than zero");
            }
        }

        std::size_t capacity() const noexcept
        {
            return this->capacity_ - 1;
        }

        bool empty() const noexcept
        {
            return this->head_.load(std::memory_order_acquire) == this->tail_.load(std::memory_order_acquire);
        }

        std::size_t size() const noexcept
        {
            const auto head = this->head_.load(std::memory_order_acquire);
            const auto tail = this->tail_.load(std::memory_order_acquire);
            if (head >= tail)
            {
                return head - tail;
            }
            return this->capacity_ - tail + head;
        }

        void push(const T& value)
        {
            this->emplace(value);
        }
        void push(T&& value)
        {
            this->emplace(std::move(value));
        }

        bool pop(T& value)
        {
            const auto tail = this->tail_.load(std::memory_order_relaxed);
            const auto head = this->head_.load(std::memory_order_acquire);
            if (tail == head)
            {
                return false;
            }

            value = std::move(this->storage_[tail]);
            this->tail_.store(this->next(tail), std::memory_order_release);
            return true;
        }

        std::vector<T> snapshot() const
        {
            std::vector<T> items;
            auto           tail = this->tail_.load(std::memory_order_acquire);
            const auto     head = this->head_.load(std::memory_order_acquire);
            while (tail != head)
            {
                items.push_back(this->storage_[tail]);
                tail = this->next(tail);
            }
            return items;
        }

    private:
        template <typename U>
        void emplace(U&& value)
        {
            const auto head      = this->head_.load(std::memory_order_relaxed);
            const auto next_head = this->next(head);
            auto       tail      = this->tail_.load(std::memory_order_acquire);

            if (next_head == tail)
            {
                this->tail_.store(this->next(tail), std::memory_order_release);
            }

            this->storage_[head] = std::forward<U>(value);
            this->head_.store(next_head, std::memory_order_release);
        }

        std::size_t next(std::size_t index) const noexcept
        {
            ++index;
            return index == this->capacity_ ? 0 : index;
        }

        const std::size_t        capacity_;
        std::vector<T>           storage_;
        std::atomic<std::size_t> head_{0};
        std::atomic<std::size_t> tail_{0};
    };

}  // namespace mcapper
