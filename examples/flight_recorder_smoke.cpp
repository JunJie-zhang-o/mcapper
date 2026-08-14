#include "recorder_rfl.hpp"

#include <rfl.hpp>

#include <cstdint>
#include <filesystem>

struct ImuSample
{
    double ax;
    double ay;
    double az;
};

int main()
{
    using namespace flightLogger;

    DoubleRingBuffer<TimedRecord<ImuSample>, 16> imu_ring;

    FlightRecorderOptions options;
    options.pre_trigger_ns = 10;
    options.post_trigger_ns = 0;
    options.mcap_metadata["robot"] = "smoke";

    FlightRecorder recorder{options};
    recorder.register_channel("/imu", imu_ring, MessageEncoding::Json);

    imu_ring.push(TimedRecord<ImuSample>{90, ImuSample{1.0, 2.0, 3.0}});
    imu_ring.push(TimedRecord<ImuSample>{100, ImuSample{4.0, 5.0, 6.0}});

    recorder.trigger(
        100,
        std::filesystem::temp_directory_path() / "flight_logger_smoke.mcap",
        "smoke");

    imu_ring.push(TimedRecord<ImuSample>{101, ImuSample{7.0, 8.0, 9.0}});
    recorder.stop();

    return 0;
}
