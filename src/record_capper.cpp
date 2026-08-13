#include <mcapper/capper.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "mcap_writer.hpp"

namespace mcapper
{

    namespace
    {

        std::uint64_t nowNs()
        {
            const auto now = std::chrono::steady_clock::now().time_since_epoch();
            return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
        }

    }  // namespace

    RecordCapper::RecordCapper(LoggerOptions options) : options_(std::move(options)), pre_trigger_buffer_(this->options_.ring_buffer_capacity)
    {
        if (this->options_.output_directory.empty())
        {
            throw std::invalid_argument("output directory must not be empty");
        }
        if (this->options_.base_filename.empty())
        {
            throw std::invalid_argument("base filename must not be empty");
        }
    }

    RecordCapper::~RecordCapper()
    {
        this->stop();
    }

    bool RecordCapper::start()
    {
        std::lock_guard<std::mutex> lock(this->worker_mutex_);
        if (this->started_)
        {
            return false;
        }

        this->worker_stop_ = false;
        this->worker_      = std::thread(&RecordCapper::workerLoop, this);
        this->started_     = true;
        return true;
    }

    bool RecordCapper::trigger()
    {
        std::vector<Record> records;
        std::uint64_t       trigger_time_ns = 0;

        {
            std::lock_guard<std::mutex> lock(this->mutex_);
            if (this->capture_active_)
            {
                return false;
            }

            trigger_time_ns = this->latest_log_time_ns_ == 0 ? nowNs() : this->latest_log_time_ns_;
            const auto cutoff =
                trigger_time_ns > this->options_.pre_trigger_duration_ns ? trigger_time_ns - this->options_.pre_trigger_duration_ns : 0;

            auto snapshot = this->pre_trigger_buffer_.snapshot();
            std::copy_if(snapshot.begin(), snapshot.end(), std::back_inserter(records), [cutoff, trigger_time_ns](const Record& record) {
                return record.log_time_ns >= cutoff && record.log_time_ns <= trigger_time_ns;
            });

            if (this->options_.post_trigger_duration_ns > 0)
            {
                this->active_capture_         = std::move(records);
                this->active_trigger_time_ns_ = trigger_time_ns;
                const auto max                = std::numeric_limits<std::uint64_t>::max();
                this->active_end_time_ns_     = max - trigger_time_ns < this->options_.post_trigger_duration_ns
                                                    ? max
                                                    : trigger_time_ns + this->options_.post_trigger_duration_ns;
                this->capture_active_         = true;
                return true;
            }
        }

        this->enqueueWrite(trigger_time_ns, std::move(records));
        return true;
    }

    void RecordCapper::stop()
    {
        std::vector<Record> records;
        std::uint64_t       trigger_time_ns = 0;
        {
            std::lock_guard<std::mutex> lock(this->mutex_);
            if (this->capture_active_)
            {
                records         = std::move(this->active_capture_);
                trigger_time_ns = this->active_trigger_time_ns_;
                this->capture_active_ = false;
            }
        }
        if (!records.empty())
        {
            this->enqueueWrite(trigger_time_ns, std::move(records));
        }

        {
            std::lock_guard<std::mutex> lock(this->worker_mutex_);
            if (!this->started_)
            {
                return;
            }
            this->worker_stop_ = true;
        }
        this->worker_cv_.notify_one();
        if (this->worker_.joinable())
        {
            this->worker_.join();
        }
        this->started_ = false;
    }

    const std::string& RecordCapper::last_output_path() const noexcept
    {
        return this->last_output_path_;
    }

    bool RecordCapper::push(const Record& record)
    {
        return this->pushNormalized(this->normalize(record));
    }

    bool RecordCapper::push(Record&& record)
    {
        return this->pushNormalized(this->normalize(std::move(record)));
    }

    bool RecordCapper::pushNormalized(Record record)
    {
        std::vector<Record> records;
        std::uint64_t       trigger_time_ns = 0;

        {
            std::lock_guard<std::mutex> lock(this->mutex_);
            this->latest_log_time_ns_ = record.log_time_ns;
            this->pre_trigger_buffer_.push(record);

            if (this->capture_active_)
            {
                if (record.log_time_ns <= this->active_end_time_ns_)
                {
                    this->active_capture_.push_back(std::move(record));
                }
                else
                {
                    records         = std::move(this->active_capture_);
                    trigger_time_ns = this->active_trigger_time_ns_;
                    this->capture_active_ = false;
                }
            }
        }

        if (!records.empty())
        {
            this->enqueueWrite(trigger_time_ns, std::move(records));
        }
        return true;
    }

    Record RecordCapper::normalize(Record record) const
    {
        if (record.publish_time_ns == 0)
        {
            record.publish_time_ns = record.log_time_ns;
        }
        if (record.topic.empty())
        {
            record.topic = this->options_.default_topic;
        }
        if (record.message_encoding.empty())
        {
            record.message_encoding = this->options_.default_message_encoding;
        }
        if (record.schema_name.empty())
        {
            record.schema_name = this->options_.default_schema_name;
        }
        if (record.schema_encoding.empty())
        {
            record.schema_encoding = this->options_.default_schema_encoding;
        }
        if (record.schema_data.empty())
        {
            record.schema_data = this->options_.default_schema_data;
        }
        return record;
    }

    void RecordCapper::enqueueWrite(std::uint64_t trigger_time_ns, std::vector<Record> records)
    {
        if (records.empty())
        {
            return;
        }

        WriteJob job{this->makeOutputPath(trigger_time_ns), std::move(records)};
        {
            std::lock_guard<std::mutex> lock(this->worker_mutex_);
            this->write_jobs_.push_back(std::move(job));
        }
        this->worker_cv_.notify_one();
    }

    std::string RecordCapper::makeOutputPath(std::uint64_t trigger_time_ns) const
    {
        std::filesystem::create_directories(this->options_.output_directory);

        std::ostringstream filename;
        filename << this->options_.base_filename << "_" << std::setw(20) << std::setfill('0') << trigger_time_ns << ".mcap";

        return (std::filesystem::path(this->options_.output_directory) / filename.str()).string();
    }

    void RecordCapper::workerLoop()
    {
        while (true)
        {
            WriteJob job;
            {
                std::unique_lock<std::mutex> lock(this->worker_mutex_);
                this->worker_cv_.wait(lock, [this] { return this->worker_stop_ || !this->write_jobs_.empty(); });
                if (this->write_jobs_.empty())
                {
                    if (this->worker_stop_)
                    {
                        return;
                    }
                    continue;
                }
                job = std::move(this->write_jobs_.front());
                this->write_jobs_.pop_front();
            }

            if (detail::writeMcap(job.path, this->options_, job.records))
            {
                std::lock_guard<std::mutex> lock(this->mutex_);
                this->last_output_path_ = job.path;
            }
        }
    }

}  // namespace mcapper
