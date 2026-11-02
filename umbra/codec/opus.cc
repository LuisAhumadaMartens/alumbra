#include "umbra/codec/opus.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/base/status.h"
#include "umbra/codec/codec_string.h"
#include "umbra/codec/reader_util.h"

namespace umbra::codec {
namespace {

constexpr std::array<std::uint8_t, 8> kMagic{'O', 'p', 'u', 's',
                                             'H', 'e', 'a', 'd'};

constexpr std::array<std::uint64_t, 9> kSurroundMasks{
    0, 0x4, 0x3, 0x7, 0x33, 0x37, 0x3f, 0x70f, 0x63f,
};

absl::Status ValidateMapping(const OpusConfig& config) {
  if (config.channel_count == 0) {
    return Corrupt("Opus configuration has no channels");
  }
  if (config.mapping_family == 0) {
    if (config.channel_count > 2) {
      return Corrupt("Opus mapping family 0 allows at most two channels");
    }
    return absl::OkStatus();
  }
  if (config.stream_count == 0 || config.coupled_count > config.stream_count ||
      config.channel_mapping.size() != config.channel_count) {
    return Corrupt("Opus channel mapping is inconsistent");
  }
  const std::size_t decoded = config.stream_count + config.coupled_count;
  if (std::ranges::any_of(config.channel_mapping,
                          [decoded](std::uint8_t entry) {
                            return entry != 255 && entry >= decoded;
                          })) {
    return Corrupt("Opus channel mapping refers to a missing stream");
  }
  return absl::OkStatus();
}

absl::Status ReadMapping(RecordReader& reader, OpusConfig& config) {
  if (config.mapping_family == 0) {
    config.stream_count = 1;
    config.coupled_count = config.channel_count == 2 ? 1 : 0;
    return absl::OkStatus();
  }
  ALUMBRA_ASSIGN_OR_RETURN(config.stream_count, reader.U8());
  ALUMBRA_ASSIGN_OR_RETURN(config.coupled_count, reader.U8());
  ALUMBRA_ASSIGN_OR_RETURN(const Bytes mapping,
                           reader.Take(config.channel_count));
  config.channel_mapping.assign(mapping.begin(), mapping.end());
  return absl::OkStatus();
}

absl::StatusOr<std::uint32_t> ReadLittleEndian(RecordReader& reader,
                                               int bytes) {
  std::uint32_t value = 0;
  for (int i = 0; i < bytes; ++i) {
    ALUMBRA_ASSIGN_OR_RETURN(const std::uint8_t byte, reader.U8());
    value |= static_cast<std::uint32_t>(byte) << (8 * i);
  }
  return value;
}

}

absl::StatusOr<OpusConfig> ParseOpusHead(Bytes data) {
  RecordReader reader(data, "OpusHead");
  ALUMBRA_ASSIGN_OR_RETURN(const Bytes magic, reader.Take(kMagic.size()));
  if (!std::equal(magic.begin(), magic.end(), kMagic.begin())) {
    return Corrupt("OpusHead signature is missing");
  }
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint8_t version, reader.U8());
  if ((version >> 4U) != 0) {
    return Unsupported(
        absl::StrCat("OpusHead version ", static_cast<int>(version)));
  }
  OpusConfig config;
  ALUMBRA_ASSIGN_OR_RETURN(config.channel_count, reader.U8());
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint32_t pre_skip,
                           ReadLittleEndian(reader, 2));
  ALUMBRA_ASSIGN_OR_RETURN(config.input_sample_rate,
                           ReadLittleEndian(reader, 4));
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint32_t gain,
                           ReadLittleEndian(reader, 2));
  config.pre_skip = static_cast<std::uint16_t>(pre_skip);
  config.output_gain = static_cast<std::int16_t>(gain);
  ALUMBRA_ASSIGN_OR_RETURN(config.mapping_family, reader.U8());
  ALUMBRA_RETURN_IF_ERROR(ReadMapping(reader, config));
  ALUMBRA_RETURN_IF_ERROR(ValidateMapping(config));
  return config;
}

absl::StatusOr<OpusConfig> ParseDOps(Bytes data) {
  RecordReader reader(data, "dOps");
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint8_t version, reader.U8());
  if (version != 0) {
    return Unsupported(
        absl::StrCat("dOps version ", static_cast<int>(version)));
  }
  OpusConfig config;
  ALUMBRA_ASSIGN_OR_RETURN(config.channel_count, reader.U8());
  ALUMBRA_ASSIGN_OR_RETURN(config.pre_skip, reader.U16());
  ALUMBRA_ASSIGN_OR_RETURN(config.input_sample_rate, reader.U32());
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint16_t gain, reader.U16());
  config.output_gain = static_cast<std::int16_t>(gain);
  ALUMBRA_ASSIGN_OR_RETURN(config.mapping_family, reader.U8());
  ALUMBRA_RETURN_IF_ERROR(ReadMapping(reader, config));
  ALUMBRA_RETURN_IF_ERROR(ValidateMapping(config));
  return config;
}

absl::StatusOr<std::vector<std::uint8_t>> SerializeDOps(
    const OpusConfig& config) {
  if (!ValidateMapping(config).ok()) {
    return absl::InvalidArgumentError("Opus configuration is inconsistent");
  }
  const auto gain = static_cast<std::uint16_t>(config.output_gain);
  std::vector<std::uint8_t> output{
      0,
      config.channel_count,
      static_cast<std::uint8_t>(config.pre_skip >> 8U),
      static_cast<std::uint8_t>(config.pre_skip),
      static_cast<std::uint8_t>(config.input_sample_rate >> 24U),
      static_cast<std::uint8_t>(config.input_sample_rate >> 16U),
      static_cast<std::uint8_t>(config.input_sample_rate >> 8U),
      static_cast<std::uint8_t>(config.input_sample_rate),
      static_cast<std::uint8_t>(gain >> 8U),
      static_cast<std::uint8_t>(gain),
      config.mapping_family};
  if (config.mapping_family != 0) {
    output.push_back(config.stream_count);
    output.push_back(config.coupled_count);
    output.insert(output.end(), config.channel_mapping.begin(),
                  config.channel_mapping.end());
  }
  return output;
}

OpusCodec ToCodec([[maybe_unused]] const OpusConfig& config) { return {}; }

penumbra::AudioInfo ToAudioInfo(const OpusConfig& config) {
  penumbra::AudioInfo info;
  info.sample_rate = kOpusOutputSampleRate;
  info.channel_count = config.channel_count;
  if (config.mapping_family <= 1 &&
      config.channel_count < kSurroundMasks.size()) {
    info.channel_mask = kSurroundMasks.at(config.channel_count);
  }
  info.sample_format = penumbra::SampleFormat::kF32;
  return info;
}

}
