#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <mcapper/flight_logger.hpp>
#include <sstream>
#include <stdexcept>

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

    FlightLogger::FlightLogger(LoggerOptions options)
        : options_(std::move(options)), pre_trigger_buffer_(this->options_.ring_buffer_capacity), trigger_(this->options_.post_trigger_duration_ns)
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

    FlightLogger::~FlightLogger()
    {
        this->stop();
    }

    bool FlightLogger::push(const Record& record)
    {
        return this->pushNormalized(this->normalize(record));
    }

    bool FlightLogger::push(Record&& record)
    {
        return this->pushNormalized(this->normalize(std::move(record)));
    }

    bool FlightLogger::trigger()
    {
        const auto trigger_time_ns = this->latest_log_time_ns_ == 0 ? nowNs() : this->latest_log_time_ns_;
        return this->trigger(trigger_time_ns);
    }

    bool FlightLogger::trigger(std::uint64_t trigger_time_ns)
    {
        if (!this->trigger_.fire(trigger_time_ns))
        {
            return false;
        }

        this->collectPreTriggerWindow(trigger_time_ns);
        this->flushed_ = false;
        return true;
    }

    bool FlightLogger::flush()
    {
        if (this->flushed_ || this->capture_.empty())
        {
            return false;
        }

        this->last_output_path_ = this->makeOutputPath(this->trigger_.trigger_time_ns());
        const auto ok           = detail::writeMcap(this->last_output_path_, this->options_, this->capture_);
        this->flushed_          = ok;
        return ok;
    }

    void FlightLogger::stop()
    {
        if (!this->stopped_)
        {
            this->flush();
            this->stopped_ = true;
        }
    }

    const LoggerOptions& FlightLogger::options() const noexcept
    {
        return this->options_;
    }

    const std::string& FlightLogger::last_output_path() const noexcept
    {
        return this->last_output_path_;
    }

    Trigger::State FlightLogger::state() const noexcept
    {
        return this->trigger_.state();
    }

    bool FlightLogger::pushNormalized(Record record)
    {
        this->latest_log_time_ns_ = record.log_time_ns;

        if (this->trigger_.active())
        {
            if (this->trigger_.accept(record.log_time_ns))
            {
                this->capture_.push_back(std::move(record));
                return true;
            }
            return this->flush();
        }

        if (!this->trigger_.complete())
        {
            this->pre_trigger_buffer_.push(std::move(record));
            return true;
        }

        return false;
    }

    void FlightLogger::collectPreTriggerWindow(std::uint64_t trigger_time_ns)
    {
        this->capture_.clear();

        const auto cutoff   = trigger_time_ns > this->options_.pre_trigger_duration_ns ? trigger_time_ns - this->options_.pre_trigger_duration_ns : 0;
        auto       snapshot = this->pre_trigger_buffer_.snapshot();
        std::copy_if(snapshot.begin(),
                     snapshot.end(),
                     std::back_inserter(this->capture_),
                     [cutoff, trigger_time_ns](const Record& record) { return record.log_time_ns >= cutoff && record.log_time_ns <= trigger_time_ns; });
    }

    Record FlightLogger::normalize(Record record) const
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

    std::string FlightLogger::makeOutputPath(std::uint64_t trigger_time_ns) const
    {
        std::filesystem::create_directories(this->options_.output_directory);

        std::ostringstream filename;
        filename << this->options_.base_filename << "_" << std::setw(20) << std::setfill('0') << trigger_time_ns << ".mcap";

        return (std::filesystem::path(this->options_.output_directory) / filename.str()).string();
    }

}  // namespace mcapper
