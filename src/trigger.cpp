#include <mcapper/trigger.hpp>

#include <limits>

namespace mcapper {

Trigger::Trigger(std::uint64_t post_trigger_duration_ns)
    : post_trigger_duration_ns_(post_trigger_duration_ns) {}

void Trigger::reset() noexcept {
  this->trigger_time_ns_ = 0;
  this->end_time_ns_ = 0;
  this->state_ = State::Idle;
}

bool Trigger::fire(std::uint64_t trigger_time_ns) noexcept {
  if (this->state_ != State::Idle) {
    return false;
  }

  this->trigger_time_ns_ = trigger_time_ns;
  const auto max = std::numeric_limits<std::uint64_t>::max();
  this->end_time_ns_ = max - trigger_time_ns < this->post_trigger_duration_ns_
                     ? max
                     : trigger_time_ns + this->post_trigger_duration_ns_;
  this->state_ = State::RecordingPostTrigger;
  return true;
}

bool Trigger::accept(std::uint64_t log_time_ns) noexcept {
  if (this->state_ != State::RecordingPostTrigger) {
    return false;
  }

  if (log_time_ns > this->end_time_ns_) {
    this->state_ = State::Complete;
    return false;
  }

  return true;
}

Trigger::State Trigger::state() const noexcept { return this->state_; }

bool Trigger::active() const noexcept { return this->state_ == State::RecordingPostTrigger; }

bool Trigger::complete() const noexcept { return this->state_ == State::Complete; }

std::uint64_t Trigger::trigger_time_ns() const noexcept { return this->trigger_time_ns_; }

std::uint64_t Trigger::end_time_ns() const noexcept { return this->end_time_ns_; }

}  // namespace mcapper
