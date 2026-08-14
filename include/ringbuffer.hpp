#progma once


namespace flightLogger
{
    

template<typename T>
struct TimedRecord
{
    uint64_t timestamp_ns;
    T data;
};





// 覆盖型环形队列
// template<typename T, std::size_t N>
// class OverwriteRingBuffer
// {
// public:
//     void push(const T& value)
//     {
//         data_[write_] = value;

//         ++write_;
//         if (write_ == N)
//             write_ = 0;

//         if (size_ < N)
//             ++size_;
//     }

// private:
//     std::array<T, N> data_;
//     std::size_t write_{0};
//     std::size_t size_{0};
// };


#include <array>
#include <cstddef>
#include <span>
#include <utility>

template<typename T, std::size_t N>
class OverwriteRingBuffer
{
    static_assert(N > 0);

public:
    struct Segments
    {
        std::span<const T> first;
        std::span<const T> second;
    };

    void push(const T& value)
    {
        data_[write_] = value;
        advance();
    }

    void push(T&& value)
    {
        data_[write_] = std::move(value);
        advance();
    }

    void clear() noexcept
    {
        write_ = 0;
        size_ = 0;
    }

    [[nodiscard]]
    std::size_t size() const noexcept
    {
        return size_;
    }

    [[nodiscard]]
    static constexpr std::size_t capacity() noexcept
    {
        return N;
    }

    [[nodiscard]]
    bool empty() const noexcept
    {
        return size_ == 0;
    }

    [[nodiscard]]
    bool full() const noexcept
    {
        return size_ == N;
    }

    // 返回按时间顺序排列的两个连续内存区间
    //
    // first + second = oldest -> newest
    [[nodiscard]]
    Segments chronological_segments() const noexcept
    {
        if (size_ == 0)
            return {};

        // 还没有发生覆盖
        if (size_ < N)
        {
            return {
                std::span<const T>{data_.data(), size_},
                {}
            };
        }

        // 已经发生覆盖：
        //
        // physical:
        //
        // [new][new][old][old][old]
        //           ^
        //         write_
        //
        // logical:
        //
        // [old][old][old] + [new][new]
        return {
            std::span<const T>{
                data_.data() + write_,
                N - write_
            },

            std::span<const T>{
                data_.data(),
                write_
            }
        };
    }

private:
    void advance() noexcept
    {
        ++write_;

        if (write_ == N)
            write_ = 0;

        if (size_ < N)
            ++size_;
    }

private:
    std::array<T, N> data_{};

    // 下一次写入的位置
    std::size_t write_{0};

    // 当前有效元素数
    std::size_t size_{0};
};



#include <atomic>
#include <cstddef>

template<typename T, std::size_t N>
class DoubleRingBuffer
{
public:
    using Ring = OverwriteRingBuffer<T, N>;

    void push(const T& value)
    {
        service_freeze_request();

        buffers_[active_].push(value);
    }

    void push(T&& value)
    {
        service_freeze_request();

        buffers_[active_].push(std::move(value));
    }

    // 可以由任意线程调用
    //
    // 注意这里只是请求 freeze，
    // 真正切换由 producer 自己执行。
    void request_freeze() noexcept
    {
        freeze_requested_.store(
            true,
            std::memory_order_release);
    }

    // MCAP线程调用
    //
    // 成功后：
    //
    // index = frozen buffer index
    // ring  = frozen ring
    //
    // 在 release_frozen() 前 producer 不会复用它。
    bool try_acquire_frozen(
        std::size_t& index,
        const Ring*& ring) noexcept
    {
        int expected =
            frozen_state_.load(
                std::memory_order_acquire);

        if (expected < 0)
            return false;

        if (!frozen_state_.compare_exchange_strong(
                expected,
                kDumping,
                std::memory_order_acq_rel,
                std::memory_order_acquire))
        {
            return false;
        }

        index = static_cast<std::size_t>(expected);
        ring = &buffers_[index];

        return true;
    }

    // MCAP写完以后释放 Frozen Buffer
    void release_frozen(std::size_t index)
    {
        buffers_[index].clear();

        frozen_state_.store(
            kNoFrozen,
            std::memory_order_release);
    }

private:
    void service_freeze_request()
    {
        // producer 自己处理切换
        if (!freeze_requested_.exchange(
                false,
                std::memory_order_acq_rel))
        {
            return;
        }

        // 上一个 frozen buffer 还没有落盘完成
        if (frozen_state_.load(
                std::memory_order_acquire) != kNoFrozen)
        {
            // 保留请求，下次 push 再尝试
            freeze_requested_.store(
                true,
                std::memory_order_release);

            return;
        }

        const std::size_t old_active = active_;
        const std::size_t new_active = 1 - active_;

        // new_active 此时一定是 Free
        active_ = new_active;

        // release：
        // 确保之前所有对 old_active 的写入
        // 对 MCAP reader 可见
        frozen_state_.store(
            static_cast<int>(old_active),
            std::memory_order_release);
    }

private:
    static constexpr int kNoFrozen = -1;
    static constexpr int kDumping  = -2;

    Ring buffers_[2];

    // 只允许 Producer Thread 修改
    std::size_t active_{0};

    // -1 : 没有 frozen
    //  0 : buffer 0 frozen
    //  1 : buffer 1 frozen
    // -2 : MCAP thread 正在 dump
    std::atomic<int> frozen_state_{kNoFrozen};

    std::atomic<bool> freeze_requested_{false};
};

}