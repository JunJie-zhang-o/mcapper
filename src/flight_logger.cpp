#define MCAP_IMPLEMENTATION

#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <limits>
#include <mcap/writer.hpp>
#include <mutex>
#include <stdexcept>
#include <thread>
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

    }  // namespace

    class FlightRecorder::Impl
    {
    public:
        explicit Impl(FlightRecorderOptions options) : options_(std::move(options))
        {
        }

        ~Impl()
        {
            this->stop_noexcept();
        }

        uint32_t allocate_channel_id()
        {
            std::lock_guard<std::mutex> lock(this->mutex_);

            if (this->running_) throw std::logic_error("cannot register channel after recorder start");

            return this->next_channel_id_++;
        }

        void register_channel(std::unique_ptr<IFlightChannel> channel)
        {
            std::lock_guard<std::mutex> lock(this->mutex_);

            if (this->running_) throw std::logic_error("cannot register channel after recorder start");

            this->channels_.push_back(std::move(channel));
        }

        void start()
        {
            std::lock_guard<std::mutex> lock(this->mutex_);

            if (this->running_) return;

            this->stopping_ = false;
            this->running_  = true;
            this->state_    = RecorderState::Armed;
            this->worker_   = std::thread([this] { this->worker_loop(); });
        }

        RecorderState state() const noexcept
        {
            std::lock_guard<std::mutex> lock(this->mutex_);
            return this->state_;
        }

        void trigger(uint64_t trigger_time_ns, std::filesystem::path output_path, std::string reason)
        {
            this->start();

            {
                std::lock_guard<std::mutex> lock(this->mutex_);

                if (this->stopping_) throw std::logic_error("cannot trigger a stopping recorder");

                this->set_state_locked(RecorderState::FreezingPreTrigger);

                for (auto& channel : this->channels_) channel->request_freeze_pre();

                this->requests_.push_back(TriggerRequest{trigger_time_ns, std::move(output_path), std::move(reason)});
            }

            this->cv_.notify_one();
        }

        void stop()
        {
            {
                std::lock_guard<std::mutex> lock(this->mutex_);

                if (!this->running_) return;

                this->stopping_ = true;
            }

            this->cv_.notify_one();

            if (this->worker_.joinable()) this->worker_.join();

            std::exception_ptr error;

            {
                std::lock_guard<std::mutex> lock(this->mutex_);
                this->running_  = false;
                this->stopping_ = false;
                this->state_    = RecorderState::Idle;
                this->requests_.clear();
                error             = this->last_error_;
                this->last_error_ = nullptr;
            }

            if (error) std::rethrow_exception(error);
        }

    private:
        struct TriggerRequest
        {
            uint64_t              trigger_time_ns;
            std::filesystem::path output_path;
            std::string           reason;
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

        void worker_loop()
        {
            for (;;)
            {
                TriggerRequest request;

                {
                    std::unique_lock<std::mutex> lock(this->mutex_);
                    this->cv_.wait(lock, [this] { return this->stopping_ || !this->requests_.empty(); });

                    if (this->stopping_ && this->requests_.empty()) return;

                    request = std::move(this->requests_.front());
                    this->requests_.pop_front();
                }

                try
                {
                    this->handle_trigger(request);
                }
                catch (...)
                {
                    std::lock_guard<std::mutex> lock(this->mutex_);
                    this->last_error_ = std::current_exception();
                    this->state_      = this->stopping_ ? RecorderState::Finalizing : RecorderState::Armed;
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
            throw_if_mcap_error(writer.open(request.output_path.string(), writer_options));

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
            auto metadata_map               = this->options_.mcap_metadata;
            metadata_map["format_version"]  = "1";
            metadata_map["trigger_time_ns"] = std::to_string(request.trigger_time_ns);
            metadata_map["begin_time_ns"]   = std::to_string(begin_time);
            metadata_map["end_time_ns"]     = std::to_string(end_time);
            metadata_map["pre_trigger_ns"]  = std::to_string(this->options_.pre_trigger_ns);
            metadata_map["post_trigger_ns"] = std::to_string(this->options_.post_trigger_ns);
            metadata_map["reason"]          = request.reason;
            metadata_map["channel_count"]   = std::to_string(this->channels_.size());

            mcap::Metadata metadata;
            metadata.name     = "flight_recorder";
            metadata.metadata = std::move(metadata_map);

            throw_if_mcap_error(writer.write(metadata));
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

        mutable std::mutex         mutex_;
        std::condition_variable    cv_;
        std::deque<TriggerRequest> requests_;
        std::thread                worker_;
        bool                       running_{false};
        bool                       stopping_{false};
        RecorderState              state_{RecorderState::Idle};
        std::exception_ptr         last_error_;
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

    void FlightRecorder::trigger(uint64_t trigger_time_ns, std::filesystem::path output_path, std::string reason)
    {
        this->impl_->trigger(trigger_time_ns, std::move(output_path), std::move(reason));
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
