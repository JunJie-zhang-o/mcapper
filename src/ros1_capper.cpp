#include <mcapper_ros1/ros1_capper.hpp>

#include <boost/function.hpp>
#include <mcapper/logger_options.hpp>
#include <ros/message_event.h>
#include <ros/serialization.h>
#include <stdexcept>
#include <topic_tools/shape_shifter.h>
#include <utility>

namespace mcapper_ros1
{

    namespace
    {

        constexpr const char* kRos1Profile         = "ros1";
        constexpr const char* kRos1MessageEncoding = "ros1";
        constexpr const char* kRos1SchemaEncoding  = "ros1msg";

        mcapper::LoggerOptions makeRos1Options(mcapper::LoggerOptions options)
        {
            options.profile                  = kRos1Profile;
            options.default_message_encoding = kRos1MessageEncoding;
            options.default_schema_encoding  = kRos1SchemaEncoding;
            return options;
        }

    }  // namespace

    struct Ros1Capper::Impl
    {
        Impl(ros::NodeHandle& node_handle, mcapper::CapperConfig config)
            : node_handle(node_handle), config(std::move(config)), recorder(makeRos1Options(this->config.logger))
        {
            if (this->config.source.type != mcapper::SourceType::Ros1)
            {
                throw std::invalid_argument("Ros1Capper requires source.type ros1");
            }
            if (this->config.source.topics.empty())
            {
                throw std::invalid_argument("Ros1Capper requires at least one topic");
            }
        }

        bool start()
        {
            if (!this->recorder.start())
            {
                return false;
            }

            this->subscribers.reserve(this->config.source.topics.size());
            for (const auto& topic : this->config.source.topics)
            {
                if (topic.empty())
                {
                    throw std::invalid_argument("Ros1Capper topic names must not be empty");
                }

                this->subscribers.push_back(this->node_handle.subscribe<topic_tools::ShapeShifter>(
                    topic, 100, boost::function<void(const ros::MessageEvent<topic_tools::ShapeShifter const>&)>(
                                    [this, topic](const ros::MessageEvent<topic_tools::ShapeShifter const>& event) { this->handleMessage(topic, event); })));
            }
            return true;
        }

        void handleMessage(const std::string& topic, const ros::MessageEvent<topic_tools::ShapeShifter const>& event)
        {
            const auto msg = event.getMessage();

            const std::string datatype = msg->getDataType();
            const std::string msg_def  = msg->getMessageDefinition();

            const auto size = ros::serialization::serializationLength(*msg);
            std::vector<std::uint8_t> bytes(size);
            ros::serialization::OStream stream(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
            ros::serialization::serialize(stream, *msg);

            mcapper::Record record;
            record.log_time_ns         = ros::Time::now().toNSec();
            record.publish_time_ns     = record.log_time_ns;
            record.topic               = topic;
            record.message_encoding    = kRos1MessageEncoding;
            record.schema_name         = datatype;
            record.schema_encoding     = kRos1SchemaEncoding;
            record.schema_data.assign(msg_def.begin(), msg_def.end());
            record.data                = std::move(bytes);

            this->recorder.push(std::move(record));
        }

        ros::NodeHandle&             node_handle;
        mcapper::CapperConfig        config;
        mcapper::RecordCapper        recorder;
        std::vector<ros::Subscriber> subscribers;
    };

    Ros1Capper::Ros1Capper(ros::NodeHandle& node_handle, mcapper::CapperConfig config)
        : impl_(std::make_unique<Impl>(node_handle, std::move(config)))
    {
    }

    Ros1Capper::~Ros1Capper() = default;

    bool Ros1Capper::start()
    {
        return this->impl_->start();
    }

    bool Ros1Capper::trigger()
    {
        return this->impl_->recorder.trigger();
    }

    void Ros1Capper::stop()
    {
        this->impl_->subscribers.clear();
        this->impl_->recorder.stop();
    }

    const std::string& Ros1Capper::last_output_path() const noexcept
    {
        return this->impl_->recorder.last_output_path();
    }

}  // namespace mcapper_ros1
