#include <array>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <mcapper/flight_logger.hpp>
#include <string>
#include <vector>

namespace
{

    std::uint64_t readU64(std::istream& in)
    {
        std::uint64_t value = 0;
        for (int i = 0; i < 8; ++i)
        {
            value |= static_cast<std::uint64_t>(static_cast<unsigned char>(in.get())) << (i * 8);
        }
        return value;
    }

    std::uint16_t readU16(const std::vector<char>& body, std::size_t offset)
    {
        return static_cast<std::uint16_t>(static_cast<unsigned char>(body[offset])) |
               static_cast<std::uint16_t>(static_cast<unsigned char>(body[offset + 1]) << 8);
    }

    std::uint64_t readBodyU64(const std::vector<char>& body, std::size_t offset)
    {
        std::uint64_t value = 0;
        for (int i = 0; i < 8; ++i)
        {
            value |= static_cast<std::uint64_t>(static_cast<unsigned char>(body[offset + i])) << (i * 8);
        }
        return value;
    }

}  // namespace

int main()
{
    mcapper::LoggerOptions options;
    options.output_directory         = "test_logs";
    options.base_filename            = "mcap_writer";
    options.pre_trigger_duration_ns  = 2'000'000'000ULL;
    options.post_trigger_duration_ns = 1'000'000'000ULL;

    mcapper::FlightLogger logger(options);
    logger.push(mcapper::Record{1'000'000'000ULL, 1'000'000'000ULL, "/imu", "raw", "Imu", "raw", {}, {1}});
    logger.push(mcapper::Record{2'000'000'000ULL, 2'000'000'000ULL, "/imu", "raw", "Imu", "raw", {}, {2}});
    assert(logger.trigger(2'000'000'000ULL));
    logger.push(mcapper::Record{2'500'000'000ULL, 2'500'000'000ULL, "/imu", "raw", "Imu", "raw", {}, {3}});
    assert(logger.flush());

    std::ifstream in(logger.last_output_path(), std::ios::binary);
    assert(in);

    const std::array<char, 8> expected_magic{static_cast<char>(0x89), 'M', 'C', 'A', 'P', '0', '\r', '\n'};
    std::array<char, 8>       magic{};
    in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    assert(magic == expected_magic);

    int           messages       = 0;
    std::uint64_t first_log_time = 0;
    std::uint64_t last_log_time  = 0;

    while (in.peek() != EOF)
    {
        const auto op     = static_cast<unsigned char>(in.get());
        const auto length = readU64(in);
        if (op == 0x89)
        {
            break;
        }

        std::vector<char> body(length);
        in.read(body.data(), static_cast<std::streamsize>(body.size()));

        if (op == 0x05)
        {
            assert(readU16(body, 0) == 1);
            const auto log_time = readBodyU64(body, 6);
            if (messages == 0)
            {
                first_log_time = log_time;
            }
            last_log_time = log_time;
            ++messages;
        }
    }

    assert(messages == 3);
    assert(first_log_time == 1'000'000'000ULL);
    assert(last_log_time == 2'500'000'000ULL);

    return 0;
}
