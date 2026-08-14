#progma once



namespace flightLogger
{


    enum class MessageEncoding
    {
        Ros1,
        Cdr,
        Protobuf,
        Flatbuffer,
        CapnProto,
        Cbor,
        MsgPack,
        Json,
    };

    enum class SchemaEncoding
    {
        None,
        Protobuf,
        Flatbuffer,
        CapnProto,
        Ros1Msg,
        Ros2Msg,
        Ros2Idl,
        OmgIdl,
        JsonSchema,
    };


    struct ChannelInfo
    {
        uint32_t id;

        std::string topic;

        std::string message_encoding;

        std::string schema_name;
        std::string schema_encoding;

        std::vector<std::byte> schema_data;
    };   


    class IFlightChannel
    {
    public:
        virtual ~IFlightChannel() = default;

        virtual uint32_t id() const noexcept = 0;

        virtual const ChannelInfo&
        info() const noexcept = 0;

        // 请求 producer 切换 buffer
        virtual void request_freeze() noexcept = 0;

        // 尝试拿到 Frozen Ring
        virtual bool acquire_frozen(
            uint64_t begin_time,
            uint64_t end_time) = 0;

        // 当前是否还有数据
        virtual bool has_current() const noexcept = 0;

        // 当前记录 timestamp
        virtual uint64_t current_timestamp() const noexcept = 0;

        // 写当前记录到 MCAP
        virtual void write_current(
            McapOutput& output) = 0;

        // 移动到下一条
        virtual void advance() noexcept = 0;

        // MCAP处理完成
        virtual void release_frozen() noexcept = 0;
    };


    template<typename T, std::size_t N>
    class FlightChannel final
        : public IFlightChannel
    {
    public:
        using Record = TimedRecord<T>;
        using DoubleRing =
            DoubleRingBuffer<Record, N>;

        FlightChannel(
            ChannelInfo info,
            DoubleRing& ring)
            : info_(std::move(info)),
            ring_(ring)
        {
        }

        uint32_t id() const noexcept override
        {
            return info_.id;
        }

        const ChannelInfo&
        info() const noexcept override
        {
            return info_;
        }

        void request_freeze() noexcept override
        {
            ring_.request_freeze();
        }

    private:
        ChannelInfo info_;
        DoubleRing& ring_;
    };


class FlightRecorder
{
public:
    template<
        typename T,
        std::size_t N
    >
    void register_channel(
        ChannelInfo info,
        DoubleRingBuffer<
            TimedRecord<T>,
            N>& ring)
    {
        channels_.push_back(
            std::make_unique<
                FlightChannel<T, N>
            >(
                std::move(info),
                ring
            )
        );
    }

private:
    std::vector<
        std::unique_ptr<IFlightChannel>
    > channels_;
};

}