#pragma once

#include <cstdint>

namespace mcapper
{

    class Trigger
    {
    public:
        enum class State
        {
            Idle,
            RecordingPostTrigger,
            Complete,
        };

        explicit Trigger(std::uint64_t post_trigger_duration_ns);

        void reset() noexcept;
        bool fire(std::uint64_t trigger_time_ns) noexcept;
        bool accept(std::uint64_t log_time_ns) noexcept;

        State         state() const noexcept;
        bool          active() const noexcept;
        bool          complete() const noexcept;
        std::uint64_t trigger_time_ns() const noexcept;
        std::uint64_t end_time_ns() const noexcept;

    private:
        std::uint64_t post_trigger_duration_ns_;
        std::uint64_t trigger_time_ns_{0};
        std::uint64_t end_time_ns_{0};
        State         state_{State::Idle};
    };

}  // namespace mcapper
