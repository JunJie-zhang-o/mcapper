#pragma once

#include <cstdint>
#include <mcapper/logger_options.hpp>
#include <mcapper/record.hpp>
#include <mcapper/ring_buffer.hpp>
#include <mcapper/trigger.hpp>
#include <string>
#include <vector>

namespace mcapper
{

    class FlightLogger
    {
    public:
        explicit FlightLogger(LoggerOptions options);
        ~FlightLogger();

        bool push(const Record& record);
        bool push(Record&& record);
        bool trigger();
        bool trigger(std::uint64_t trigger_time_ns);
        bool flush();
        void stop();

        const LoggerOptions& options() const noexcept;
        const std::string&   last_output_path() const noexcept;
        Trigger::State       state() const noexcept;

    private:
        bool        pushNormalized(Record record);
        void        collectPreTriggerWindow(std::uint64_t trigger_time_ns);
        Record      normalize(Record record) const;
        std::string makeOutputPath(std::uint64_t trigger_time_ns) const;

        LoggerOptions       options_;
        RingBuffer<Record>  pre_trigger_buffer_;
        Trigger             trigger_;
        std::vector<Record> capture_;
        std::uint64_t       latest_log_time_ns_{0};
        std::string         last_output_path_;
        bool                flushed_{false};
        bool                stopped_{false};
    };

}  // namespace mcapper
