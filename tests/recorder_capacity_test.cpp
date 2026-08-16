#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "recorder.hpp"

namespace
{
    struct Sample
    {
        uint32_t value;
    };

    struct ParsedMcap
    {
        std::vector<uint64_t> message_times;
        std::unordered_map<std::string, std::unordered_map<std::string, std::string>> metadata;
    };

    void fail(const std::string& message)
    {
        throw std::runtime_error(message);
    }

    void expect(bool condition, const std::string& message)
    {
        if (!condition) fail(message);
    }

    flightLogger::SerializedPayload serialize_sample(const Sample& sample)
    {
        flightLogger::SerializedPayload out(sizeof(sample.value));
        auto* bytes = reinterpret_cast<unsigned char*>(out.data());
        for (std::size_t i = 0; i < sizeof(sample.value); ++i)
        {
            bytes[i] = static_cast<unsigned char>((sample.value >> (8 * i)) & 0xff);
        }
        return out;
    }

    std::vector<unsigned char> read_file(const std::filesystem::path& path)
    {
        std::ifstream input{path, std::ios::binary | std::ios::ate};
        if (!input) fail("failed to open mcap: " + path.string());

        const auto end_position = input.tellg();
        if (end_position == std::streampos{-1}) fail("failed to stat mcap: " + path.string());

        std::vector<unsigned char> data(static_cast<std::size_t>(end_position));
        input.seekg(0);
        input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!input) fail("failed to read mcap: " + path.string());

        return data;
    }

    uint16_t read_u16(const std::vector<unsigned char>& data, std::size_t& offset, std::size_t end)
    {
        if (offset + 2 > end) fail("truncated uint16");

        uint16_t value = 0;
        for (std::size_t i = 0; i < 2; ++i) value |= static_cast<uint16_t>(data[offset + i]) << (8 * i);

        offset += 2;
        return value;
    }

    uint32_t read_u32(const std::vector<unsigned char>& data, std::size_t& offset, std::size_t end)
    {
        if (offset + 4 > end) fail("truncated uint32");

        uint32_t value = 0;
        for (std::size_t i = 0; i < 4; ++i) value |= static_cast<uint32_t>(data[offset + i]) << (8 * i);

        offset += 4;
        return value;
    }

    uint64_t read_u64(const std::vector<unsigned char>& data, std::size_t& offset, std::size_t end)
    {
        if (offset + 8 > end) fail("truncated uint64");

        uint64_t value = 0;
        for (std::size_t i = 0; i < 8; ++i) value |= static_cast<uint64_t>(data[offset + i]) << (8 * i);

        offset += 8;
        return value;
    }

    std::string read_string(const std::vector<unsigned char>& data, std::size_t& offset, std::size_t end)
    {
        const auto size = read_u32(data, offset, end);
        if (offset + size > end) fail("truncated string");

        std::string value{reinterpret_cast<const char*>(data.data() + offset), size};
        offset += size;
        return value;
    }

    std::unordered_map<std::string, std::string> read_string_map(const std::vector<unsigned char>& data, std::size_t& offset, std::size_t end)
    {
        const auto map_size = read_u32(data, offset, end);
        if (offset + map_size > end) fail("truncated string map");

        const auto map_end = offset + map_size;
        std::unordered_map<std::string, std::string> result;
        while (offset < map_end)
        {
            auto key   = read_string(data, offset, map_end);
            auto value = read_string(data, offset, map_end);
            result.emplace(std::move(key), std::move(value));
        }

        return result;
    }

    void parse_records(const std::vector<unsigned char>& data, std::size_t& offset, std::size_t end, ParsedMcap& parsed)
    {
        constexpr unsigned char kMessageOpCode  = 0x05;
        constexpr unsigned char kChunkOpCode    = 0x06;
        constexpr unsigned char kMetadataOpCode = 0x0C;
        constexpr std::size_t   kHeaderSize     = 9;

        while (offset + kHeaderSize <= end)
        {
            const auto record_opcode = data[offset++];
            const auto record_size   = read_u64(data, offset, end);
            if (record_size > end - offset) fail("record extends past mcap data section");

            const auto record_end = offset + static_cast<std::size_t>(record_size);
            if (record_opcode == kMessageOpCode)
            {
                std::size_t record_offset = offset;
                (void)read_u16(data, record_offset, record_end);
                (void)read_u32(data, record_offset, record_end);
                parsed.message_times.push_back(read_u64(data, record_offset, record_end));
            }
            else if (record_opcode == kMetadataOpCode)
            {
                std::size_t record_offset = offset;
                auto        name          = read_string(data, record_offset, record_end);
                auto        metadata      = read_string_map(data, record_offset, record_end);
                parsed.metadata.emplace(std::move(name), std::move(metadata));
            }
            else if (record_opcode == kChunkOpCode)
            {
                std::size_t record_offset = offset;
                (void)read_u64(data, record_offset, record_end);
                (void)read_u64(data, record_offset, record_end);
                (void)read_u64(data, record_offset, record_end);
                (void)read_u32(data, record_offset, record_end);
                const auto compression = read_string(data, record_offset, record_end);
                const auto records_size = read_u64(data, record_offset, record_end);
                if (!compression.empty()) fail("compressed chunks are not supported by this test parser");
                if (record_offset + records_size > record_end) fail("chunk records extend past chunk");

                auto chunk_offset = record_offset;
                parse_records(data, chunk_offset, record_offset + static_cast<std::size_t>(records_size), parsed);
            }

            offset = record_end;
        }
    }

    ParsedMcap parse_mcap(const std::filesystem::path& path)
    {
        constexpr std::size_t kMagicSize = 8;

        const auto data = read_file(path);
        if (data.size() < 2 * kMagicSize) fail("mcap is too small");

        ParsedMcap parsed;
        std::size_t offset = kMagicSize;
        parse_records(data, offset, data.size() - kMagicSize, parsed);

        return parsed;
    }

    std::filesystem::path find_single_mcap(const std::filesystem::path& output_dir)
    {
        std::vector<std::filesystem::path> mcaps;
        for (const auto& entry : std::filesystem::directory_iterator{output_dir})
        {
            if (entry.path().extension() == ".mcap") mcaps.push_back(entry.path());
        }

        if (mcaps.size() != 1) fail("expected exactly one mcap file");
        return mcaps.front();
    }

    flightLogger::ChannelInfo sample_channel_info()
    {
        flightLogger::ChannelInfo info;
        info.topic            = "/sample";
        info.message_encoding = "raw";
        info.schema_name      = "Sample";
        info.schema_encoding  = "raw";
        return info;
    }

    std::filesystem::path make_output_dir(const std::string& name)
    {
        const auto output_dir = std::filesystem::temp_directory_path() /
                                ("flight_logger_" + name + "_" +
                                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(output_dir);
        return output_dir;
    }

    void push_sample(flightLogger::BlackBox<flightLogger::TimedRecord<Sample>>& ring, uint64_t timestamp, uint32_t value)
    {
        ring.push(flightLogger::TimedRecord<Sample>{timestamp, Sample{value}});
    }

    void records_pre_tail_and_first_post_capacity()
    {
        const auto output_dir = make_output_dir("capacity");

        flightLogger::FlightRecorderOptions options;
        options.output_path               = (output_dir / "capacity").string();
        options.pre_capacity              = 2;
        options.post_capacity             = 3;
        options.post_trigger_timeout_ms   = 2000;

        flightLogger::BlackBox<flightLogger::TimedRecord<Sample>> ring{2, 3};
        flightLogger::FlightRecorder recorder{options};
        recorder.register_channel<Sample>(sample_channel_info(), ring, serialize_sample);

        push_sample(ring, 10, 1);
        push_sample(ring, 20, 2);
        push_sample(ring, 30, 3);

        recorder.trigger(35, "capacity");
        push_sample(ring, 40, 4);
        push_sample(ring, 50, 5);
        push_sample(ring, 60, 6);
        push_sample(ring, 70, 7);
        recorder.stop();

        const auto parsed = parse_mcap(find_single_mcap(output_dir));
        expect(parsed.message_times == std::vector<uint64_t>({20, 30, 40, 50, 60}), "unexpected capacity-driven message timestamps");

        const auto options_it = parsed.metadata.find("RECORDER_OPTIONS");
        expect(options_it != parsed.metadata.end(), "missing RECORDER_OPTIONS metadata");
        expect(options_it->second.at("/sample.pre_capacity") == "2", "missing per-topic pre capacity metadata");
        expect(options_it->second.at("/sample.post_capacity") == "3", "missing per-topic post capacity metadata");

        std::filesystem::remove_all(output_dir);
    }

    void timeout_flushes_partial_post_capacity()
    {
        const auto output_dir = make_output_dir("timeout");

        flightLogger::FlightRecorderOptions options;
        options.output_path             = (output_dir / "timeout").string();
        options.pre_capacity            = 2;
        options.post_capacity           = 3;
        options.post_trigger_timeout_ms = 20;

        flightLogger::BlackBox<flightLogger::TimedRecord<Sample>> ring{2, 3};
        flightLogger::FlightRecorder recorder{options};
        recorder.register_channel<Sample>(sample_channel_info(), ring, serialize_sample);

        push_sample(ring, 10, 1);
        push_sample(ring, 20, 2);
        push_sample(ring, 30, 3);

        recorder.trigger(35, "timeout");
        push_sample(ring, 40, 4);
        recorder.stop();

        const auto parsed = parse_mcap(find_single_mcap(output_dir));
        expect(parsed.message_times == std::vector<uint64_t>({20, 30, 40}), "timeout did not flush partial post data");

        std::filesystem::remove_all(output_dir);
    }
}  // namespace

int main()
{
    try
    {
        records_pre_tail_and_first_post_capacity();
        timeout_flushes_partial_post_capacity();
    }
    catch (const std::exception& error)
    {
        fail(error.what());
    }

    return 0;
}
