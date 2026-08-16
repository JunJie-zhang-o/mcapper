#define MCAP_IMPLEMENTATION

#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstddef>
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

        std::string media_type_for_path(const std::filesystem::path& path)
        {
            auto extension = path.extension().string();
            for (char& ch : extension) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

            switch (extension.size())
            {
                case 4:
                    if (extension == ".txt" || extension == ".log") return "text/plain";
                    if (extension == ".yml") return "application/yaml";
                    if (extension == ".xml") return "application/xml";
                    if (extension == ".png") return "image/png";
                    if (extension == ".jpg") return "image/jpeg";
                    if (extension == ".pdf") return "application/pdf";
                    break;
                case 5:
                    if (extension == ".json") return "application/json";
                    if (extension == ".yaml") return "application/yaml";
                    if (extension == ".urdf") return "application/xml";
                    if (extension == ".jpeg") return "image/jpeg";
                    break;
                default:
                    break;
            }

            return "application/octet-stream";
        }

        std::vector<std::byte> read_attachment_data(const std::filesystem::path& path)
        {
            std::ifstream input{path, std::ios::binary | std::ios::ate};
            if (!input) throw std::runtime_error("cannot open attachment file: " + path.string());

            const auto end_position = input.tellg();
            if (end_position == std::streampos{-1}) throw std::runtime_error("cannot stat attachment file: " + path.string());

            std::vector<std::byte> data(static_cast<std::size_t>(end_position));
            input.seekg(0);

            if (!data.empty())
            {
                input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
                if (!input) throw std::runtime_error("cannot read attachment file: " + path.string());
            }

            return data;
        }

    }  // namespace

    class FlightRecorder::Impl
    {
    public:
        struct AttachmentInfo
        {
            std::filesystem::path path;
            std::string           name;
        };

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

        void add_attachment(std::filesystem::path path, std::string name)
        {
            std::lock_guard<std::mutex> lock(this->mutex_);

            this->throw_if_registration_closed_locked();

            if (name.empty()) name = path.filename().string();

            this->attachments_.push_back(AttachmentInfo{std::move(path), std::move(name)});
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

            mcap::McapWriterOptions writer_options{this->options_.profile};
            writer_options.library     = this->options_.library;
            writer_options.compression = mcap::Compression::None;

            mcap::McapWriter writer;
            const auto       output_path = this->make_output_path();
            throw_if_mcap_error(writer.open(output_path.string(), writer_options));

            const auto channel_ids = this->register_mcap_channels(writer);
            this->write_metadata(writer, request);
            this->write_attachments(writer, request.trigger_time_ns);

            this->acquire_all(FrozenWindow::PreTrigger);
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

            try
            {
                this->set_state(RecorderState::PostTrigger);
                this->wait_for_post_capacity_or_timeout();

                this->set_state(RecorderState::FreezingPostTrigger);
                for (auto& channel : this->channels_) channel->request_freeze_post();

                this->acquire_all(FrozenWindow::PostTrigger);
                this->set_state(RecorderState::Dumping);
                this->write_acquired_records(writer, channel_ids);

                this->set_state(RecorderState::Finalizing);
                this->release_all(FrozenWindow::PostTrigger);
            }
            catch (...)
            {
                this->release_all(FrozenWindow::PostTrigger);
                this->release_all(FrozenWindow::PreTrigger);
                throw;
            }

            this->release_all(FrozenWindow::PreTrigger);
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
                metadata["pre_capacity"]             = std::to_string(flight_channel->pre_capacity());
                metadata["post_capacity"]            = std::to_string(flight_channel->post_capacity());

                mcap::Channel channel{info.topic, info.message_encoding, schema.id, metadata};
                writer.addChannel(channel);
                channel_ids.emplace(info.id, channel.id);
            }

            return channel_ids;
        }

        void write_attachments(mcap::McapWriter& writer, uint64_t timestamp)
        {
            for (const auto& info : this->attachments_)
            {
                const auto data = read_attachment_data(info.path);

                mcap::Attachment attachment;
                attachment.logTime    = timestamp;
                attachment.createTime = timestamp;
                attachment.name       = info.name;
                attachment.mediaType  = media_type_for_path(info.path);
                attachment.dataSize   = data.size();
                attachment.data       = data.data();

                throw_if_mcap_error(writer.write(attachment));
            }
        }

        void acquire_all(FrozenWindow window)
        {
            std::vector<bool> acquired(this->channels_.size(), false);
            std::size_t       acquired_count = 0;

            while (acquired_count < this->channels_.size())
            {
                for (std::size_t i = 0; i < this->channels_.size(); ++i)
                {
                    if (acquired[i]) continue;

                    const bool channel_acquired = window == FrozenWindow::PreTrigger ? this->channels_[i]->acquire_frozen_pre()
                                                                                     : this->channels_[i]->acquire_frozen_post();

                    if (channel_acquired)
                    {
                        acquired[i] = true;
                        ++acquired_count;
                    }
                }

                if (acquired_count < this->channels_.size()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        void wait_for_post_capacity_or_timeout()
        {
            const auto timeout = std::chrono::nanoseconds(this->options_.post_trigger_timeout_ns);
            const auto start   = std::chrono::steady_clock::now();

            while (!this->all_post_buffers_full())
            {
                if (std::chrono::steady_clock::now() - start >= timeout) return;

                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        bool all_post_buffers_full() const
        {
            for (const auto& channel : this->channels_)
            {
                if (!channel->post_full()) return false;
            }

            return true;
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

        void write_metadata(mcap::McapWriter& writer, const TriggerRequest& request)
        {
            write_metadata_record(writer, "OS_INFO", make_os_info_metadata());

            std::unordered_map<std::string, std::string> trigger_info;
            trigger_info["trigger_time"]    = format_wall_time_ms(request.trigger_wall_time);
            trigger_info["trigger_time_ns"] = std::to_string(request.trigger_time_ns);
            trigger_info["reason"]          = request.reason;
            write_metadata_record(writer, "TRIGGER_INFO", std::move(trigger_info));

            auto recorder_options               = this->options_.mcap_metadata;
            recorder_options["format_version"]  = "1";
            recorder_options["pre_capacity"]    = std::to_string(this->options_.pre_capacity);
            recorder_options["post_capacity"]   = std::to_string(this->options_.post_capacity);
            recorder_options["post_trigger_timeout_ns"] = std::to_string(this->options_.post_trigger_timeout_ns);
            recorder_options["output_path"]      = this->options_.output_path;
            recorder_options["output_file_name"] = this->options_.output_file_name;
            recorder_options["profile"]          = this->options_.profile;
            recorder_options["library"]          = this->options_.library;
            recorder_options["channel_count"]    = std::to_string(this->channels_.size());
            for (const auto& channel : this->channels_)
            {
                const auto& topic = channel->info().topic;
                recorder_options[topic + ".pre_capacity"]  = std::to_string(channel->pre_capacity());
                recorder_options[topic + ".post_capacity"] = std::to_string(channel->post_capacity());
            }
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
        std::vector<AttachmentInfo>                  attachments_;
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

    void FlightRecorder::add_attachment(std::filesystem::path path, std::string name)
    {
        this->add_attachment_path(std::move(path), std::move(name));
    }

    void FlightRecorder::add_attachment(const std::string& path, std::string name)
    {
        this->add_attachment_path(std::filesystem::path{path}, std::move(name));
    }

    void FlightRecorder::add_attachment(const char* path, std::string name)
    {
        this->add_attachment_path(std::filesystem::path{path}, std::move(name));
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

    void FlightRecorder::add_attachment_path(std::filesystem::path path, std::string name)
    {
        this->impl_->add_attachment(std::move(path), std::move(name));
    }

}  // namespace flightLogger
