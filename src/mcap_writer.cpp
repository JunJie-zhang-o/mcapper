#include "mcap_writer.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace mcapper::detail {

namespace {

constexpr std::array<std::uint8_t, 8> kMagic{0x89, 'M', 'C', 'A', 'P', '0', '\r', '\n'};

enum class OpCode : std::uint8_t {
  Header = 0x01,
  Footer = 0x02,
  Schema = 0x03,
  Channel = 0x04,
  Message = 0x05,
  Statistics = 0x0b,
  SummaryOffset = 0x0e,
  DataEnd = 0x0f,
};

struct ChannelInfo {
  std::uint16_t schema_id;
  std::uint16_t channel_id;
};

struct SchemaInfo {
  std::uint16_t id;
  std::string name;
  std::string encoding;
  std::vector<std::uint8_t> data;
};

struct ChannelSummary {
  std::uint16_t id;
  std::uint16_t schema_id;
  std::string topic;
  std::string message_encoding;
};

struct SummaryOffsetInfo {
  OpCode opcode;
  std::uint64_t start;
  std::uint64_t length;
};

void writeU16(std::ostream& out, std::uint16_t value) {
  out.put(static_cast<char>(value & 0xff));
  out.put(static_cast<char>((value >> 8) & 0xff));
}

void writeU32(std::ostream& out, std::uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    out.put(static_cast<char>((value >> (i * 8)) & 0xff));
  }
}

void writeU64(std::ostream& out, std::uint64_t value) {
  for (int i = 0; i < 8; ++i) {
    out.put(static_cast<char>((value >> (i * 8)) & 0xff));
  }
}

void writeBytes(std::ostream& out, const std::vector<std::uint8_t>& bytes) {
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void writeString(std::ostream& out, const std::string& value) {
  writeU32(out, static_cast<std::uint32_t>(value.size()));
  out.write(value.data(), static_cast<std::streamsize>(value.size()));
}

void writeByteArray(std::ostream& out, const std::vector<std::uint8_t>& value) {
  writeU32(out, static_cast<std::uint32_t>(value.size()));
  writeBytes(out, value);
}

void writeEmptyMap(std::ostream& out) {
  writeU32(out, 0);
}

template <typename Writer>
std::uint64_t writeRecord(std::ostream& out, OpCode op_code, Writer writer) {
  std::ostringstream body;
  writer(body);
  const auto data = body.str();
  out.put(static_cast<char>(op_code));
  writeU64(out, static_cast<std::uint64_t>(data.size()));
  out.write(data.data(), static_cast<std::streamsize>(data.size()));
  return static_cast<std::uint64_t>(1 + 8 + data.size());
}

std::string schemaKey(const Record& record) {
  return record.schema_name + '\0' + record.schema_encoding +
         std::string(reinterpret_cast<const char*>(record.schema_data.data()), record.schema_data.size());
}

std::string channelKey(const Record& record, std::uint16_t schema_id) {
  return std::to_string(schema_id) + '\0' + record.topic + '\0' + record.message_encoding;
}

void requireWritable(const std::ofstream& out, const std::string& path) {
  if (!out) {
    throw std::runtime_error("failed to write MCAP file: " + path);
  }
}

void writeSchema(std::ostream& out, const SchemaInfo& schema) {
  writeU16(out, schema.id);
  writeString(out, schema.name);
  writeString(out, schema.encoding);
  writeByteArray(out, schema.data);
}

void writeChannel(std::ostream& out, const ChannelSummary& channel) {
  writeU16(out, channel.id);
  writeU16(out, channel.schema_id);
  writeString(out, channel.topic);
  writeString(out, channel.message_encoding);
  writeEmptyMap(out);
}

}  // namespace

bool writeMcap(const std::string& path,
               const LoggerOptions& options,
               const std::vector<Record>& records) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return false;
  }

  out.write(reinterpret_cast<const char*>(kMagic.data()), static_cast<std::streamsize>(kMagic.size()));

  writeRecord(out, OpCode::Header, [&options](std::ostream& body) {
    writeString(body, options.profile);
    writeString(body, options.library);
  });

  std::unordered_map<std::string, std::uint16_t> schemas;
  std::unordered_map<std::string, ChannelInfo> channels;
  std::vector<SchemaInfo> schema_summaries;
  std::vector<ChannelSummary> channel_summaries;
  std::unordered_map<std::uint16_t, std::uint64_t> channel_message_counts;
  std::uint16_t next_schema_id = 1;
  std::uint16_t next_channel_id = 1;
  std::uint64_t message_count = 0;
  std::uint64_t message_start_time = 0;
  std::uint64_t message_end_time = 0;

  for (const auto& record : records) {
    const auto schema_key = schemaKey(record);
    auto schema_it = schemas.find(schema_key);
    if (schema_it == schemas.end()) {
      const auto schema_id = next_schema_id++;
      schema_it = schemas.emplace(schema_key, schema_id).first;
      schema_summaries.push_back(SchemaInfo{schema_id, record.schema_name, record.schema_encoding, record.schema_data});
      writeRecord(out, OpCode::Schema, [&record, schema_id](std::ostream& body) {
        writeSchema(body, SchemaInfo{schema_id, record.schema_name, record.schema_encoding, record.schema_data});
      });
    }

    const auto channel_key = channelKey(record, schema_it->second);
    auto channel_it = channels.find(channel_key);
    if (channel_it == channels.end()) {
      const ChannelInfo info{schema_it->second, next_channel_id++};
      channel_it = channels.emplace(channel_key, info).first;
      channel_summaries.push_back(ChannelSummary{info.channel_id, info.schema_id, record.topic, record.message_encoding});
      writeRecord(out, OpCode::Channel, [&record, info](std::ostream& body) {
        writeChannel(body, ChannelSummary{info.channel_id, info.schema_id, record.topic, record.message_encoding});
      });
    }

    writeRecord(out, OpCode::Message, [&record, channel_it](std::ostream& body) {
      writeU16(body, channel_it->second.channel_id);
      writeU32(body, 0);
      writeU64(body, record.log_time_ns);
      writeU64(body, record.publish_time_ns);
      writeBytes(body, record.data);
    });

    if (message_count == 0) {
      message_start_time = record.log_time_ns;
      message_end_time = record.log_time_ns;
    } else {
      message_start_time = std::min(message_start_time, record.log_time_ns);
      message_end_time = std::max(message_end_time, record.log_time_ns);
    }
    ++message_count;
    ++channel_message_counts[channel_it->second.channel_id];
  }

  writeRecord(out, OpCode::DataEnd, [](std::ostream& body) {
    writeU32(body, 0);
  });

  const auto summary_start = static_cast<std::uint64_t>(out.tellp());
  std::ostringstream summary;
  std::vector<SummaryOffsetInfo> summary_offsets;

  auto writeSummaryGroup = [&summary, summary_start, &summary_offsets](OpCode opcode, auto write_group) {
    const auto group_start = static_cast<std::uint64_t>(summary.tellp());
    write_group();
    const auto group_end = static_cast<std::uint64_t>(summary.tellp());
    if (group_end != group_start) {
      summary_offsets.push_back(SummaryOffsetInfo{opcode, summary_start + group_start, group_end - group_start});
    }
  };

  writeSummaryGroup(OpCode::Schema, [&]() {
    for (const auto& schema : schema_summaries) {
      writeRecord(summary, OpCode::Schema, [&schema](std::ostream& body) {
        writeSchema(body, schema);
      });
    }
  });

  writeSummaryGroup(OpCode::Channel, [&]() {
    for (const auto& channel : channel_summaries) {
      writeRecord(summary, OpCode::Channel, [&channel](std::ostream& body) {
        writeChannel(body, channel);
      });
    }
  });

  writeSummaryGroup(OpCode::Statistics, [&]() {
    writeRecord(summary, OpCode::Statistics, [&](std::ostream& body) {
      writeU64(body, message_count);
      writeU16(body, static_cast<std::uint16_t>(schema_summaries.size()));
      writeU32(body, static_cast<std::uint32_t>(channel_summaries.size()));
      writeU32(body, 0);
      writeU32(body, 0);
      writeU32(body, 0);
      writeU64(body, message_start_time);
      writeU64(body, message_end_time);
      writeU32(body, static_cast<std::uint32_t>(channel_message_counts.size() * 10));
      for (const auto& entry : channel_message_counts) {
        writeU16(body, entry.first);
        writeU64(body, entry.second);
      }
    });
  });

  const auto summary_offset_start = summary_start + static_cast<std::uint64_t>(summary.tellp());
  for (const auto& offset : summary_offsets) {
    writeRecord(summary, OpCode::SummaryOffset, [&offset](std::ostream& body) {
      body.put(static_cast<char>(offset.opcode));
      writeU64(body, offset.start);
      writeU64(body, offset.length);
    });
  }

  const auto summary_data = summary.str();
  out.write(summary_data.data(), static_cast<std::streamsize>(summary_data.size()));

  writeRecord(out, OpCode::Footer, [summary_start, summary_offset_start](std::ostream& body) {
    writeU64(body, summary_start);
    writeU64(body, summary_offset_start);
    writeU32(body, 0);
  });

  out.write(reinterpret_cast<const char*>(kMagic.data()), static_cast<std::streamsize>(kMagic.size()));
  requireWritable(out, path);
  return true;
}

}  // namespace mcapper::detail
