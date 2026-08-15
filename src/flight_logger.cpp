#define MCAP_IMPLEMENTATION

#include <chrono>
#include <condition_variable>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mcap/writer.hpp>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <sys/utsname.h>
#include <thread>
#include <unordered_map>
#include <vector>

#include "recorder.hpp"

namespace flightLogger
{
    namespace
    {

        void throw_if_mcap_error(const mcap::Status& status)
        {
            if (!status.ok()) throw std::runtime_error("MCAP writer error: " + status.message);
        }

        uint64_t saturated_add(uint64_t lhs, uint64_t rhs) noexcept
        {
            if (std::numeric_limits<uint64_t>::max() - lhs < rhs) return std::numeric_limits<uint64_t>::max();

            return lhs + rhs;
        }

        uint64_t now_steady_ns() noexcept
        {
            using namespace std::chrono;
            return static_cast<uint64_t>(duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
        }

        std::string unquote_os_release_value(std::string value)
        {
            if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\'')))
            {
                value = value.substr(1, value.size() - 2);
            }

            return value;
        }

        std::string read_os_pretty_name()
        {
            std::ifstream os_release{"/etc/os-release"};
            std::string   line;

            while (std::getline(os_release, line))
            {
                constexpr std::string_view key{"PRETTY_NAME="};
                if (line.starts_with(key)) return unquote_os_release_value(line.substr(key.size()));
            }

            return {};
        }

        std::unordered_map<std::string, std::string> make_os_info_metadata()
        {
            struct utsname uts_info
            {
            };

            const bool has_uname = ::uname(&uts_info) == 0;

            std::unordered_map<std::string, std::string> metadata;
            metadata["version"] = read_os_pretty_name();
            if (metadata["version"].empty()) metadata["version"] = has_uname ? uts_info.sysname : "unknown";
            metadata["kernel"] = has_uname ? uts_info.release : "unknown";
            metadata["arch"]   = has_uname ? uts_info.machine : "unknown";

            return metadata;
        }

        std::string format_wall_time_ms(std::chrono::system_clock::time_point time_point)
        {
            using namespace std::chrono;

            const auto time_t_s = system_clock::to_time_t(time_point);
            const auto ms_part  = duration_cast<milliseconds>(time_point.time_since_epoch()) % 1000;

            std::tm tm_buf{};
            ::localtime_r(&time_t_s, &tm_buf);

            std::ostringstream oss;
            oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << ms_part.count();

            return oss.str();
        }

        void write_metadata_record(mcap::McapWriter& writer, std::string name, std::unordered_map<std::string, std::string> metadata_map)
        {
            mcap::Metadata metadata;
            metadata.name     = std::move(name);
            metadata.metadata = std::move(metadata_map);

            throw_if_mcap_error(writer.write(metadata));
        }

    }  // namespace

    class FlightRecorder::Impl
    {
    public:
        explicit Impl(FlightRecorderOptions options) : options_(std::move(options))
        {
            this->start_worker();
        }

        ~Impl()
        {
            this->stop_noexcept();
        }

        uint32_t allocate_channel_id()
        {
            std::lock_guard<std::mutex> lock(this->mutex_);

            this->throw_if_registration_closed_locked();

            return this->next_channel_id_++;
        }

        void register_channel(std::unique_ptr<IFlightChannel> channel)
        {
            std::lock_guard<std::mutex> lock(this->mutex_);

            this->throw_if_registration_closed_locked();

            this->channels_.push_back(std::move(channel));
        }

        void start()
        {
            this->start_worker();
        }

        RecorderState state() const noexcept
        {
            std::lock_guard<std::mutex> lock(this->mutex_);
            return this->state_;
        }

        void trigger(uint64_t trigger_time_ns, std::string reason)
        {
            this->start();
            const auto trigger_wall_time = std::chrono::system_clock::now();

            {
                std::lock_guard<std::mutex> lock(this->mutex_);

                if (this->stopping_) throw std::logic_error("cannot trigger a stopping recorder");

                if (this->state_ != RecorderState::Armed) throw std::logic_error("recorder is busy handling a previous trigger");

                this->registration_closed_ = true;
                this->set_state_locked(RecorderState::FreezingPreTrigger);

                for (auto& channel : this->channels_) channel->request_freeze_pre();

                this->pending_request_.emplace(TriggerRequest{trigger_time_ns, std::move(reason), trigger_wall_time});
            }

            this->cv_.notify_one();
        }

        void stop()
        {
            {
                std::lock_guard<std::mutex> lock(this->mutex_);

                if (!this->worker_.joinable()) return;

                this->stopping_ = true;
                this->worker_.request_stop();
            }

            this->cv_.notify_one();

            if (this->worker_.joinable()) this->worker_.join();

            std::exception_ptr error;

            {
                std::lock_guard<std::mutex> lock(this->mutex_);
                this->stopping_            = false;
                this->registration_closed_ = false;
                this->state_               = RecorderState::Idle;
                this->pending_request_.reset();
                error             = this->last_error_;
                this->last_error_ = nullptr;
            }

            if (error) std::rethrow_exception(error);
        }

    private:
        struct TriggerRequest
        {
            uint64_t                              trigger_time_ns;
            std::string                           reason;
            std::chrono::system_clock::time_point trigger_wall_time;
        };

        enum class FrozenWindow
        {
            PreTrigger,
            PostTrigger,
        };

        void stop_noexcept() noexcept
        {
            try
            {
                this->stop();
            }
            catch (...)
            {
            }
        }

        void start_worker()
        {
            std::lock_guard<std::mutex> lock(this->mutex_);

            if (this->worker_.joinable()) return;

            this->stopping_ = false;
            this->state_    = RecorderState::Armed;
            this->worker_   = std::jthread([this](std::stop_token stop_token) { this->worker_loop(stop_token); });
        }

        void throw_if_registration_closed_locked() const
        {
            if (this->stopping_) throw std::logic_error("cannot register channel while recorder is stopping");

            if (this->registration_closed_) throw std::logic_error("cannot register channel after recorder trigger");

            if (this->state_ != RecorderState::Idle && this->state_ != RecorderState::Armed)
                throw std::logic_error("cannot register channel while recorder is busy");
        }

        void worker_loop(std::stop_token stop_token)
        {
            for (;;)
            {
                TriggerRequest request;

                {
                    std::unique_lock<std::mutex> lock(this->mutex_);
                    this->cv_.wait(lock, stop_token, [this] { return this->pending_request_.has_value(); });

                    if (!this->pending_request_.has_value()) return;

                    request = std::move(*this->pending_request_);
                    this->pending_request_.reset();
                }

                try
                {
                    this->handle_trigger(request);
                }
                catch (...)
                {
                    std::lock_guard<std::mutex> lock(this->mutex_);
                    this->last_error_ = std::current_exception();
                    this->state_      = stop_token.stop_requested() ? RecorderState::Finalizing : RecorderState::Armed;
                }
            }
        }

        void handle_trigger(const TriggerRequest& request)
        {
            this->set_state(RecorderState::FreezingPreTrigger);

            const uint64_t begin_time = request.trigger_time_ns > this->options_.pre_trigger_ns ? request.trigger_time_ns - this->options_.pre_trigger_ns : 0;
            const uint64_t end_time   = saturated_add(request.trigger_time_ns, this->options_.post_trigger_ns);

            mcap::McapWriterOptions writer_options{this->options_.profile};
            writer_options.library     = this->options_.library;
            writer_options.compression = mcap::Compression::None;

            mcap::McapWriter writer;
            const auto       output_path = this->make_output_path();
            throw_if_mcap_error(writer.open(output_path.string(), writer_options));

            const auto channel_ids = this->register_mcap_channels(writer);
            this->write_metadata(writer, request, begin_time, end_time);

            this->acquire_all(FrozenWindow::PreTrigger, begin_time, request.trigger_time_ns);
            this->set_state(RecorderState::Dumping);
            try
            {
                this->write_acquired_records(writer, channel_ids);
            }
            catch (...)
            {
                this->release_all(FrozenWindow::PreTrigger);
                throw;
            }

            const uint64_t post_begin =
                request.trigger_time_ns == std::numeric_limits<uint64_t>::max() ? request.trigger_time_ns : request.trigger_time_ns + 1;

            if (post_begin <= end_time)
            {
                this->release_all(FrozenWindow::PreTrigger);

                this->set_state(RecorderState::PostTrigger);
                std::this_thread::sleep_for(std::chrono::nanoseconds(this->options_.post_trigger_ns));

                this->set_state(RecorderState::FreezingPostTrigger);
                for (auto& channel : this->channels_) channel->request_freeze_post();

                this->acquire_all(FrozenWindow::PostTrigger, post_begin, end_time);
                this->set_state(RecorderState::Dumping);
                try
                {
                    this->write_acquired_records(writer, channel_ids);
                }
                catch (...)
                {
                    this->release_all(FrozenWindow::PostTrigger);
                    throw;
                }

                this->set_state(RecorderState::Finalizing);
                this->release_all(FrozenWindow::PostTrigger);
            }
            else
            {
                this->set_state(RecorderState::Finalizing);
                this->release_all(FrozenWindow::PreTrigger);
            }

            writer.close();
            this->set_state(RecorderState::Armed);
        }

        std::unordered_map<uint32_t, mcap::ChannelId> register_mcap_channels(mcap::McapWriter& writer)
        {
            std::unordered_map<uint32_t, mcap::ChannelId> channel_ids;

            for (const auto& flight_channel : this->channels_)
            {
                const auto& info = flight_channel->info();

                mcap::Schema schema{info.schema_name, info.schema_encoding, info.schema_data};
                writer.addSchema(schema);

                auto metadata                        = info.metadata;
                metadata["flight_logger_channel_id"] = std::to_string(info.id);
                metadata["message_encoding"]         = info.message_encoding;
                metadata["schema_name"]              = info.schema_name;
                metadata["schema_encoding"]          = info.schema_encoding;
                metadata["ring_capacity"]            = std::to_string(flight_channel->ring_capacity());

                mcap::Channel channel{info.topic, info.message_encoding, schema.id, metadata};
                writer.addChannel(channel);
                channel_ids.emplace(info.id, channel.id);
            }

            return channel_ids;
        }

        void acquire_all(FrozenWindow window, uint64_t begin_time, uint64_t end_time)
        {
            std::vector<bool> acquired(this->channels_.size(), false);
            std::size_t       acquired_count = 0;

            while (acquired_count < this->channels_.size())
            {
                for (std::size_t i = 0; i < this->channels_.size(); ++i)
                {
                    if (acquired[i]) continue;

                    const bool channel_acquired = window == FrozenWindow::PreTrigger ? this->channels_[i]->acquire_frozen_pre(begin_time, end_time)
                                                                                     : this->channels_[i]->acquire_frozen_post(begin_time, end_time);

                    if (channel_acquired)
                    {
                        acquired[i] = true;
                        ++acquired_count;
                    }
                }

                if (acquired_count < this->channels_.size()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        void write_acquired_records(mcap::McapWriter& writer, const std::unordered_map<uint32_t, mcap::ChannelId>& channel_ids)
        {
            for (;;)
            {
                IFlightChannel* next = nullptr;

                for (auto& channel : this->channels_)
                {
                    if (!channel->has_current()) continue;

                    if (!next || channel->current_timestamp() < next->current_timestamp())
                    {
                        next = channel.get();
                    }
                }

                if (!next) return;

                const auto payload = next->serialize_current();

                mcap::Message message;
                message.channelId   = channel_ids.at(next->id());
                message.sequence    = 0;
                message.logTime     = next->current_timestamp();
                message.publishTime = next->current_timestamp();
                message.dataSize    = payload.size();
                message.data        = payload.data();

                throw_if_mcap_error(writer.write(message));
                next->advance();
            }
        }

        void release_all(FrozenWindow window) noexcept
        {
            for (auto& channel : this->channels_)
            {
                if (window == FrozenWindow::PreTrigger)
                    channel->release_frozen_pre();
                else
                    channel->release_frozen_post();
            }
        }

        void write_metadata(mcap::McapWriter& writer, const TriggerRequest& request, uint64_t begin_time, uint64_t end_time)
        {
            write_metadata_record(writer, "OS_INFO", make_os_info_metadata());

            std::unordered_map<std::string, std::string> trigger_info;
            trigger_info["trigger_time"]    = format_wall_time_ms(request.trigger_wall_time);
            trigger_info["trigger_time_ns"] = std::to_string(request.trigger_time_ns);
            trigger_info["reason"]          = request.reason;
            write_metadata_record(writer, "TRIGGER_INFO", std::move(trigger_info));

            auto recorder_options               = this->options_.mcap_metadata;
            recorder_options["format_version"]  = "1";
            recorder_options["begin_time_ns"]   = std::to_string(begin_time);
            recorder_options["end_time_ns"]     = std::to_string(end_time);
            recorder_options["pre_trigger_ns"]  = std::to_string(this->options_.pre_trigger_ns);
            recorder_options["post_trigger_ns"] = std::to_string(this->options_.post_trigger_ns);
            recorder_options["output_path"]      = this->options_.output_path;
            recorder_options["output_file_name"] = this->options_.output_file_name;
            recorder_options["profile"]          = this->options_.profile;
            recorder_options["library"]          = this->options_.library;
            recorder_options["channel_count"]    = std::to_string(this->channels_.size());
            write_metadata_record(writer, "RECORDER_OPTIONS", std::move(recorder_options));
        }

        std::filesystem::path make_output_path() const
        {
            std::filesystem::path output_dir{this->options_.output_path};
            std::filesystem::create_directories(output_dir);

            // 生成形如 "前缀-年月日_时分秒" 的时间戳(本地时间,秒精度)。
            using namespace std::chrono;

            const auto now      = system_clock::now();
            const auto time_t_s = system_clock::to_time_t(now);

            std::tm tm_buf{};
            ::localtime_r(&time_t_s, &tm_buf);

            std::ostringstream oss;
            oss << std::put_time(&tm_buf, "%Y%m%d_%H%M%S");

            return output_dir / (this->options_.output_file_name + "-" + oss.str() + ".mcap");
        }

        void set_state(RecorderState state) noexcept
        {
            std::lock_guard<std::mutex> lock(this->mutex_);
            this->set_state_locked(state);
        }

        void set_state_locked(RecorderState state) noexcept
        {
            this->state_ = state;
        }

    private:
        FlightRecorderOptions                        options_;
        std::vector<std::unique_ptr<IFlightChannel>> channels_;
        uint32_t                                     next_channel_id_{1};

        mutable std::mutex            mutex_;
        std::condition_variable_any   cv_;
        std::optional<TriggerRequest> pending_request_;
        std::jthread                  worker_;
        bool                          stopping_{false};
        bool                          registration_closed_{false};
        RecorderState                 state_{RecorderState::Idle};
        std::exception_ptr            last_error_;
    };

    FlightRecorder::FlightRecorder(FlightRecorderOptions options) : impl_(std::make_unique<Impl>(std::move(options)))
    {
    }

    FlightRecorder::~FlightRecorder() = default;

    void FlightRecorder::start()
    {
        this->impl_->start();
    }

    RecorderState FlightRecorder::state() const noexcept
    {
        return this->impl_->state();
    }

    void FlightRecorder::trigger(uint64_t trigger_time_ns, std::string reason)
    {
        this->impl_->trigger(trigger_time_ns, std::move(reason));
    }

    void FlightRecorder::trigger(std::string reason)
    {
        this->impl_->trigger(now_steady_ns(), std::move(reason));
    }

    void FlightRecorder::stop()
    {
        this->impl_->stop();
    }

    uint32_t FlightRecorder::allocate_channel_id()
    {
        return this->impl_->allocate_channel_id();
    }

    void FlightRecorder::register_channel_erased(std::unique_ptr<IFlightChannel> channel)
    {
        this->impl_->register_channel(std::move(channel));
    }

}  // namespace flightLogger
