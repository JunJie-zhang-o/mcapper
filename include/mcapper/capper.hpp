#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mcapper/logger_options.hpp>
#include <mcapper/record.hpp>
#include <mcapper/ring_buffer.hpp>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mcapper
{

    class Capper
    {
    public:
        virtual ~Capper() = default;

        virtual bool               start()                           = 0;
        virtual bool               trigger()                         = 0;
        virtual void               stop()                            = 0;
        virtual const std::string& last_output_path() const noexcept = 0;
    };

    class RecordCapper final : public Capper
    {
    public:
        explicit RecordCapper(LoggerOptions options);
        ~RecordCapper() override;

        bool               start() override;
        bool               trigger() override;
        void               stop() override;
        const std::string& last_output_path() const noexcept override;

        bool push(const Record& record);
        bool push(Record&& record);

    private:
        struct WriteJob
        {
            std::string         path;
            std::vector<Record> records;
        };

        bool        pushNormalized(Record record);
        Record      normalize(Record record) const;
        void        enqueueWrite(std::uint64_t trigger_time_ns, std::vector<Record> records);
        std::string makeOutputPath(std::uint64_t trigger_time_ns) const;
        void        workerLoop();

        LoggerOptions      options_;
        RingBuffer<Record> pre_trigger_buffer_;

        mutable std::mutex  mutex_;
        bool                started_{false};
        bool                capture_active_{false};
        std::uint64_t       active_trigger_time_ns_{0};
        std::uint64_t       active_end_time_ns_{0};
        std::uint64_t       latest_log_time_ns_{0};
        std::vector<Record> active_capture_;
        std::string         last_output_path_;

        std::mutex              worker_mutex_;
        std::condition_variable worker_cv_;
        std::deque<WriteJob>    write_jobs_;
        std::thread             worker_;
        bool                    worker_stop_{false};
    };

}  // namespace mcapper
