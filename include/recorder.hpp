#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "channel.hpp"
#include "codec.hpp"

namespace flightLogger
{

    enum class RecorderState
    {
        Idle,
        Armed,
        FreezingPreTrigger,
        PostTrigger,
        FreezingPostTrigger,
        Dumping,
        Finalizing,
    };

    struct FlightRecorderOptions
    {
        std::size_t                                  pre_capacity{4096};
        std::size_t                                  post_capacity{4096};
        uint64_t                                     post_trigger_timeout_ms{1000};
        std::string                                  output_path{"flight_logger"};
        std::unordered_map<std::string, std::string> mcap_metadata;

        void validate() const;
    };

    namespace detail
    {
        inline std::size_t parse_capacity_metadata(const std::unordered_map<std::string, std::string>& metadata,
                                                   const std::string&                                  key,
                                                   std::size_t                                        fallback)
        {
            const auto it = metadata.find(key);
            if (it == metadata.end()) return fallback;

            std::size_t       parsed = 0;
            const auto        value  = std::stoull(it->second, &parsed);
            constexpr uint64_t max_size = static_cast<uint64_t>(std::numeric_limits<std::size_t>::max());
            if (parsed != it->second.size() || value == 0 || value > max_size)
            {
                throw std::invalid_argument("invalid positive integer channel metadata value for " + key + ": " + it->second);
            }

            return static_cast<std::size_t>(value);
        }

        inline void apply_channel_capacity_metadata(ChannelInfo& info, std::size_t pre_capacity, std::size_t post_capacity)
        {
            const auto metadata_pre_capacity  = parse_capacity_metadata(info.metadata, "pre_capacity", pre_capacity);
            const auto metadata_post_capacity = parse_capacity_metadata(info.metadata, "post_capacity", post_capacity);

            if (metadata_pre_capacity != pre_capacity)
                throw std::invalid_argument("channel metadata pre_capacity does not match ring pre_capacity");

            if (metadata_post_capacity != post_capacity)
                throw std::invalid_argument("channel metadata post_capacity does not match ring post_capacity");

            info.metadata["pre_capacity"]  = std::to_string(pre_capacity);
            info.metadata["post_capacity"] = std::to_string(post_capacity);
        }
    }  // namespace detail

    class FlightRecorder
    {
    public:
        explicit FlightRecorder(FlightRecorderOptions options);
        ~FlightRecorder();

        FlightRecorder(const FlightRecorder&)            = delete;
        FlightRecorder& operator=(const FlightRecorder&) = delete;

        // =====================================================================
        // register_channel 系列重载
        // ---------------------------------------------------------------------
        // 提供四种由低到高的注册方式,内部逐层转发,最终都汇聚到 (1) 号重载:
        //   (1) 传入自定义 ISerializer<T> 智能指针      —— 最灵活,最底层
        //   (2) 传入一个 std::function / lambda        —— 中间便捷层
        //   (3) 传入 ICodec<T> 智能指针               —— 自动填充 schema/channel 信息
        //   (4) 只传 topic 与 MessageEncoding 枚举     —— 最简 API,自动建 codec
        // =====================================================================

        /**
         * @brief (1) 底层重载:使用调用方自行构造的 ISerializer<T>。
         *
         * 该重载真正执行注册动作:分配唯一 channel id,构造 FlightChannel,
         * 再通过类型擦除接口交给内部 Impl 管理。其它重载最终都会转发到这里。
         *
         * @tparam T           消息类型
         * @param  ring        与生产者共享的三缓冲环 BlackBox
         * @param  info        通道信息(id 字段会被内部覆盖,无需填写)
         * @param  ring        与生产者共享的三缓冲环 BlackBox
         * @param  serializer  std::unique_ptr<ISerializer<T>>,负责把 T 序列化为字节
         *
         * @code
         * // 用户自定义一个 ISerializer 派生类
         * class MyProtoSerializer : public flightLogger::ISerializer<MyMsg> {
         *     flightLogger::SerializedPayload serialize(const MyMsg& m) override {
         *         // ... 自定义高性能编码 ...
         *     }
         * };
         *
         * flightLogger::ChannelInfo info;
         * info.topic            = "/imu";
         * info.message_encoding = "protobuf";
         * info.schema_name      = "MyMsg";
         * // schema_encoding / schema_data 视需要填写
         *
         * recorder.register_channel<MyMsg>(
         *     std::move(info), ring, std::make_unique<MyProtoSerializer>());
         * @endcode
         */
        template <typename T>
        void register_channel(ChannelInfo info, BlackBox<TimedRecord<T>>& ring, typename FlightChannel<T>::Serializer serializer)
        {
            info.id = this->allocate_channel_id();
            detail::apply_channel_capacity_metadata(info, ring.pre_capacity(), ring.post_capacity());
            this->register_channel_erased(std::make_unique<FlightChannel<T>>(std::move(info), ring, std::move(serializer)));
        }

        /**
         * @brief (2) 便捷重载:直接传入一个可调用对象(lambda / std::function)。
         *
         * 内部会用 FunctionSerializer<T> 把该可调用对象包装成 ISerializer<T>,
         * 然后转发到 (1) 号重载。适合“临时写一个编码函数”的场景,免去派生一个类。
         *
         * @tparam T           消息类型
         * @param  info        通道信息(id 字段无需填写)
         * @param  ring        三缓冲环
         * @param  serializer  形如 `SerializedPayload(const T&)` 的可调用对象
         *
         * @code
         * flightLogger::ChannelInfo info;
         * info.topic            = "/debug/counter";
         * info.message_encoding = "raw";
         * info.schema_name      = "uint64";
         *
         * recorder.register_channel<uint64_t>(
         *     std::move(info), ring,
         *     [](const uint64_t& v) -> flightLogger::SerializedPayload {
         *         flightLogger::SerializedPayload out(sizeof(v));
         *         std::memcpy(out.data(), &v, sizeof(v));
         *         return out;
         *     });
         * @endcode
         */
        template <typename T>
        void register_channel(ChannelInfo info, BlackBox<TimedRecord<T>>& ring, typename FunctionSerializer<T>::Function serializer)
        {
            this->register_channel<T>(std::move(info), ring, std::make_unique<FunctionSerializer<T>>(std::move(serializer)));
        }

        /**
         * @brief (3) Codec 重载:传入已构造的 ICodec 派生对象,自动填充 ChannelInfo。
         *
         * 适合 ROS1 / ROS2 / struct 等需要调用方显式选择 codec 或提供 schema 的场景。
         */
        template <typename T, typename Codec, typename = std::enable_if_t<std::is_base_of_v<ICodec<T>, Codec>>>
        void register_channel(std::string topic,
                              BlackBox<TimedRecord<T>>& ring,
                              std::unique_ptr<Codec> codec,
                              std::unordered_map<std::string, std::string> metadata = {})
        {
            if (!codec) throw std::invalid_argument("flight channel codec is empty");

            ChannelInfo info;
            auto        schema = codec->schema();

            info.topic             = std::move(topic);
            info.message_encoding  = codec->message_encoding();
            info.schema_name       = std::move(schema.name);
            info.schema_encoding   = std::move(schema.encoding);
            info.schema_data       = std::move(schema.data);
            info.metadata          = std::move(metadata);
            info.metadata["topic"] = info.topic;

            this->register_channel<T>(std::move(info), ring, std::move(codec));
        }

        /**
         * @brief (4) 高层重载:只需指定 topic 和 MessageEncoding,库自动完成其余配置。
         *
         * 内部行为:
         *   1. 通过 `detail::make_codec<T>(encoding)` 创建对应编码的 codec
         *      (codec 派生自 ICodec<T> ⊂ ISerializer<T>);
         *   2. 从 codec 中提取 SchemaInfo(name / encoding / data)自动填入 ChannelInfo;
         *   3. 转发到 (1) 号重载完成注册。
         *
         * 这是最常用、最推荐的入口——调用方无需关心 schema 生成、无需手写序列化函数。
         *
         * @tparam T         消息类型(必须被对应 codec 支持,例如 JSON 需可反射)
         * @param  topic     MCAP 中的 topic 名(例如 "/imu/data")
         * @param  ring      三缓冲环
         * @param  encoding  期望的消息编码格式(JSON / CBOR / MsgPack / ...)
         *
         * @code
         * struct ImuData { double ax, ay, az; uint64_t stamp; };
         *
         * flightLogger::BlackBox<flightLogger::TimedRecord<ImuData>> ring{1024, 256};
         *
         * recorder.register_channel<ImuData>(
         *     "/imu/data", ring, flightLogger::MessageEncoding::Json);
         * @endcode
         */
        template <typename T>
        void register_channel(std::string topic,
                              BlackBox<TimedRecord<T>>& ring,
                              MessageEncoding encoding,
                              std::unordered_map<std::string, std::string> metadata = {})
        {
            using Value = std::remove_cvref_t<T>;

            ChannelInfo info;
            auto        codec  = detail::make_codec<Value>(encoding);
            auto        schema = codec->schema();

            info.topic             = std::move(topic);
            info.message_encoding  = codec->message_encoding();
            info.schema_name       = std::move(schema.name);
            info.schema_encoding   = std::move(schema.encoding);
            info.schema_data       = std::move(schema.data);
            info.metadata          = std::move(metadata);
            info.metadata["topic"] = info.topic;

            this->register_channel<Value>(std::move(info), ring, std::move(codec));
        }

        void start();

        RecorderState state() const noexcept;

        void add_attachment(std::filesystem::path path, std::string name = {});
        void add_attachment(const std::string& path, std::string name = {});
        void add_attachment(const char* path, std::string name = {});

        void trigger(std::string reason);
        void trigger(uint64_t trigger_time_ns, std::string reason = {});

        void stop();

    private:
        class Impl;

        uint32_t allocate_channel_id();
        void     register_channel_erased(std::unique_ptr<IFlightChannel> channel);
        void     add_attachment_path(std::filesystem::path path, std::string name);

    private:
        std::unique_ptr<Impl> impl_;
    };

}  // namespace flightLogger
