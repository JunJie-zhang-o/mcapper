#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include "ringbuffer.hpp"

namespace
{
    void fail(const std::string& message)
    {
        throw std::runtime_error(message);
    }

    void expect(bool condition, const std::string& message)
    {
        if (!condition) fail(message);
    }

    template <typename T>
    void append_segment(std::vector<T>& out, flightLogger::ConstSegment<T> values)
    {
        for (const auto& value : values) out.push_back(value);
    }

    template <typename T>
    std::vector<T> values_from(const flightLogger::OverwriteRingBuffer<T>& ring)
    {
        std::vector<T> values;
        const auto     segments = ring.chronological_segments();
        append_segment(values, segments.first);
        append_segment(values, segments.second);
        return values;
    }

    void overwrite_ring_uses_runtime_capacity()
    {
        flightLogger::OverwriteRingBuffer<int> ring{3};

        ring.push(1);
        ring.push(2);
        ring.push(3);
        ring.push(4);

        const auto values = values_from(ring);
        expect(values == std::vector<int>({2, 3, 4}), "overwrite ring did not keep the latest values");
        expect(ring.capacity() == 3, "unexpected overwrite ring capacity");
    }

    void blackbox_uses_pre_and_post_capacities()
    {
        flightLogger::BlackBox<int> ring{2, 3};

        ring.push(1);
        ring.push(2);
        ring.push(3);
        ring.request_freeze_pre();

        ring.push(4);
        ring.push(5);
        ring.push(6);
        ring.push(7);
        expect(ring.post_full(), "post buffer did not report full");

        std::size_t index = 0;
        const flightLogger::OverwriteRingBuffer<int>* frozen = nullptr;
        expect(ring.try_acquire_frozen_pre(index, frozen), "failed to acquire pre buffer");
        expect(values_from(*frozen) == std::vector<int>({2, 3}), "pre buffer did not keep the latest pre records");

        ring.request_freeze_post();
        const flightLogger::OverwriteRingBuffer<int>* post = nullptr;
        expect(ring.try_acquire_frozen_post(index, post), "failed to acquire post buffer");
        expect(values_from(*post) == std::vector<int>({4, 5, 6}), "post buffer did not keep the first post records");

        ring.release_frozen_post(index);
        ring.release_frozen_pre(0);

        expect(ring.pre_capacity() == 2, "unexpected pre capacity");
        expect(ring.post_capacity() == 3, "unexpected post capacity");
    }
}  // namespace

int main()
{
    try
    {
        overwrite_ring_uses_runtime_capacity();
        blackbox_uses_pre_and_post_capacities();
    }
    catch (const std::exception& error)
    {
        fail(error.what());
    }

    return 0;
}
