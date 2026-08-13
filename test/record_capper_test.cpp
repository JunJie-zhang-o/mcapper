#include <cassert>
#include <filesystem>
#include <fstream>
#include <mcapper/capper.hpp>
#include <mcapper/logger_options.hpp>
#include <mcapper/record.hpp>
#include <string>
#include <vector>

namespace
{

    bool exists(const std::string& path)
    {
        std::ifstream in(path, std::ios::binary);
        return static_cast<bool>(in);
    }

}  // namespace

int main()
{
    mcapper::LoggerOptions options;
    options.output_directory         = "test_logs";
    options.base_filename            = "record_capper";
    options.pre_trigger_duration_ns  = 2'000'000'000ULL;
    options.post_trigger_duration_ns = 0;
    options.ring_buffer_capacity     = 10;

    mcapper::RecordCapper capper(options);
    assert(capper.start());
    assert(!capper.start());

    assert(capper.push(mcapper::Record{1'000'000'000ULL, 1'000'000'000ULL, "/imu", "raw", "Imu", "raw", {}, {1}}));
    assert(capper.push(mcapper::Record{2'000'000'000ULL, 2'000'000'000ULL, "/imu", "raw", "Imu", "raw", {}, {2}}));
    assert(capper.trigger());

    assert(capper.push(mcapper::Record{5'000'000'000ULL, 5'000'000'000ULL, "/imu", "raw", "Imu", "raw", {}, {3}}));
    assert(capper.trigger());

    capper.stop();

    const auto first_path  = std::filesystem::path("test_logs") / "record_capper_00000000002000000000.mcap";
    const auto second_path = std::filesystem::path("test_logs") / "record_capper_00000000005000000000.mcap";
    assert(exists(first_path.string()));
    assert(exists(second_path.string()));
    assert(capper.last_output_path() == second_path.string());

    return 0;
}
