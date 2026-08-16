#include <cstdint>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "recorder.hpp"

namespace
{
    struct AttachmentRecord
    {
        std::string media_type;
        uint64_t    data_size;
    };

    void fail(const std::string& message)
    {
        throw std::runtime_error(message);
    }

    void write_file(const std::filesystem::path& path, const std::string& data)
    {
        std::ofstream output{path, std::ios::binary};
        if (!output) fail("failed to create test file: " + path.string());

        output.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!output) fail("failed to write test file: " + path.string());
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

    std::unordered_map<std::string, AttachmentRecord> read_attachment_records(const std::filesystem::path& path)
    {
        constexpr unsigned char kAttachmentOpCode = 0x09;
        constexpr std::size_t   kMagicSize        = 8;
        constexpr std::size_t   kRecordHeaderSize = 9;

        const auto data = read_file(path);
        if (data.size() < 2 * kMagicSize) fail("mcap is too small");

        std::unordered_map<std::string, AttachmentRecord> attachments;
        std::size_t                                       offset = kMagicSize;
        const std::size_t                                 end    = data.size() - kMagicSize;

        while (offset + kRecordHeaderSize <= end)
        {
            const auto opcode = data[offset++];
            const auto size   = read_u64(data, offset, end);
            if (size > end - offset) fail("record extends past mcap data section");

            const std::size_t record_end = offset + static_cast<std::size_t>(size);

            if (opcode == kAttachmentOpCode)
            {
                std::size_t record_offset = offset;
                (void)read_u64(data, record_offset, record_end);
                (void)read_u64(data, record_offset, record_end);
                const auto name       = read_string(data, record_offset, record_end);
                const auto media_type = read_string(data, record_offset, record_end);
                const auto data_size  = read_u64(data, record_offset, record_end);

                if (data_size > record_end - record_offset) fail("attachment data extends past record");
                record_offset += static_cast<std::size_t>(data_size);
                (void)read_u32(data, record_offset, record_end);

                attachments.emplace(name, AttachmentRecord{media_type, data_size});
            }

            offset = record_end;
        }

        return attachments;
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

    void expect_attachment(const std::unordered_map<std::string, AttachmentRecord>& attachments,
                           const std::string&                                      name,
                           const std::string&                                      media_type,
                           uint64_t                                                data_size)
    {
        const auto it = attachments.find(name);
        if (it == attachments.end()) fail("missing attachment: " + name);
        if (it->second.media_type != media_type)
            fail("unexpected media type for " + name + ": " + it->second.media_type);
        if (it->second.data_size != data_size) fail("unexpected data size for " + name);
    }
}  // namespace

int main()
{
    try
    {
        using namespace flightLogger;

        const auto base_dir = std::filesystem::temp_directory_path() /
                              ("flight_logger_attachment_test_" +
                               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        const auto input_dir  = base_dir / "input";
        const auto output_dir = base_dir / "output";
        std::filesystem::create_directories(input_dir);
        std::filesystem::create_directories(output_dir);

        const std::unordered_map<std::string, std::string> contents{
            {"config.JSON", "{\"rate\": 100}"},
            {"events.log", "boot ok\n"},
            {"notes.TXT", "plain text\n"},
            {"calibration.YML", "gain: 1\n"},
            {"robot.urdf", "<robot/>\n"},
            {"layout.xml", "<root/>\n"},
            {"image.PNG", "png bytes"},
            {"photo.JPEG", "jpeg bytes"},
            {"manual.pdf", "%PDF-test\n"},
            {"blob.bin", "unknown bytes"},
        };

        for (const auto& [name, data] : contents) write_file(input_dir / name, data);

        FlightRecorderOptions options;
        options.output_path = (output_dir / "attachments").string();

        FlightRecorder recorder{options};
        recorder.add_attachment(input_dir / "config.JSON");

        const std::string log_path = (input_dir / "events.log").string();
        recorder.add_attachment(log_path, "renamed.log");

        const std::string unknown_path = (input_dir / "blob.bin").string();
        recorder.add_attachment(unknown_path.c_str());

        recorder.add_attachment(input_dir / "notes.TXT");
        recorder.add_attachment(input_dir / "calibration.YML");
        recorder.add_attachment(input_dir / "robot.urdf");
        recorder.add_attachment(input_dir / "layout.xml");
        recorder.add_attachment(input_dir / "image.PNG");
        recorder.add_attachment(input_dir / "photo.JPEG");
        recorder.add_attachment(input_dir / "manual.pdf");

        recorder.trigger(12345, "attachment test");
        recorder.stop();

        const auto attachments = read_attachment_records(find_single_mcap(output_dir));
        expect_attachment(attachments, "config.JSON", "application/json", contents.at("config.JSON").size());
        expect_attachment(attachments, "renamed.log", "text/plain", contents.at("events.log").size());
        expect_attachment(attachments, "notes.TXT", "text/plain", contents.at("notes.TXT").size());
        expect_attachment(attachments, "calibration.YML", "application/yaml", contents.at("calibration.YML").size());
        expect_attachment(attachments, "robot.urdf", "application/xml", contents.at("robot.urdf").size());
        expect_attachment(attachments, "layout.xml", "application/xml", contents.at("layout.xml").size());
        expect_attachment(attachments, "image.PNG", "image/png", contents.at("image.PNG").size());
        expect_attachment(attachments, "photo.JPEG", "image/jpeg", contents.at("photo.JPEG").size());
        expect_attachment(attachments, "manual.pdf", "application/pdf", contents.at("manual.pdf").size());
        expect_attachment(attachments, "blob.bin", "application/octet-stream", contents.at("blob.bin").size());

        std::filesystem::remove_all(base_dir);
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
