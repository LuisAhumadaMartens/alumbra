#include "penumbra/desktop/decoder_config.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "penumbra/desktop/decode_errors.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/codec/av1.h"
#include "umbra/codec/flac.h"
#include "umbra/codec/opus.h"

namespace penumbra::desktop {
namespace {

constexpr std::string_view kOpusHeadMagic = "OpusHead";
constexpr size_t kDfLaStreamInfoOffset = 8;

bool StartsWith(std::span<const uint8_t> bytes, std::string_view prefix) {
  return bytes.size() >= prefix.size() &&
         std::equal(prefix.begin(), prefix.end(), bytes.begin(),
                    [](char expected, uint8_t actual) {
                      return static_cast<uint8_t>(expected) == actual;
                    });
}

void AppendLittleEndian(std::vector<uint8_t>& out, uint32_t value,
                        size_t bytes) {
  for (size_t index = 0; index < bytes; ++index) {
    out.push_back(static_cast<uint8_t>(value >> (8 * index)));
  }
}

absl::StatusOr<std::vector<uint8_t>> OpusHeadFrom(
    std::span<const uint8_t> config) {
  if (StartsWith(config, kOpusHeadMagic)) {
    auto parsed = umbra::codec::ParseOpusHead(config);
    if (!parsed.ok()) {
      return parsed.status();
    }
    return std::vector<uint8_t>(config.begin(), config.end());
  }
  auto parsed = umbra::codec::ParseDOps(config);
  if (!parsed.ok()) {
    return parsed.status();
  }
  const umbra::codec::OpusConfig& opus = *parsed;
  std::vector<uint8_t> head(kOpusHeadMagic.begin(), kOpusHeadMagic.end());
  head.push_back(1);
  head.push_back(opus.channel_count);
  AppendLittleEndian(head, opus.pre_skip, 2);
  AppendLittleEndian(head, opus.input_sample_rate, 4);
  AppendLittleEndian(head, static_cast<uint16_t>(opus.output_gain), 2);
  head.push_back(opus.mapping_family);
  if (opus.mapping_family != 0) {
    head.push_back(opus.stream_count);
    head.push_back(opus.coupled_count);
    head.insert(head.end(), opus.channel_mapping.begin(),
                opus.channel_mapping.end());
  }
  return head;
}

absl::StatusOr<std::vector<uint8_t>> FlacStreamInfoFrom(
    std::span<const uint8_t> config) {
  if (config.size() == umbra::codec::kFlacStreamInfoSize) {
    auto parsed = umbra::codec::ParseFlacStreamInfo(config);
    if (!parsed.ok()) {
      return parsed.status();
    }
    return std::vector<uint8_t>(config.begin(), config.end());
  }
  auto parsed = umbra::codec::ParseDfLa(config);
  if (!parsed.ok()) {
    return parsed.status();
  }
  const auto begin = config.begin() + kDfLaStreamInfoOffset;
  return std::vector<uint8_t>(begin, begin + umbra::codec::kFlacStreamInfoSize);
}

}

absl::StatusOr<std::vector<uint8_t>> FfmpegExtradata(const StreamInfo& info) {
  const std::span<const uint8_t> config = info.codec_config.span();
  switch (info.codec) {
    case CodecId::kH264:
    case CodecId::kHevc:
    case CodecId::kAac:
      return std::vector<uint8_t>(config.begin(), config.end());
    case CodecId::kOpus:
      if (config.empty()) {
        return CorruptSampleError("an Opus stream without a configuration");
      }
      return OpusHeadFrom(config);
    case CodecId::kFlac:
      if (config.empty()) {
        return CorruptSampleError("a FLAC stream without a configuration");
      }
      return FlacStreamInfoFrom(config);
    case CodecId::kVp9:
    case CodecId::kVp8:
    case CodecId::kAc3:
    case CodecId::kEac3:
    case CodecId::kMp3:
      return std::vector<uint8_t>();
    case CodecId::kVorbis:
    case CodecId::kTheora:
      if (config.empty()) {
        return CorruptSampleError("a codec stream without a configuration");
      }
      return std::vector<uint8_t>(config.begin(), config.end());
    case CodecId::kAv1:
    case CodecId::kUnknown:
      break;
  }
  return CodecUnsupportedError("no FFmpeg decoder for this codec");
}

absl::StatusOr<std::vector<uint8_t>> Av1ConfigObus(const StreamInfo& info) {
  if (info.codec != CodecId::kAv1) {
    return CodecUnsupportedError("not an AV1 stream");
  }
  if (info.codec_config.empty()) {
    return std::vector<uint8_t>();
  }
  auto parsed = umbra::codec::ParseAv1C(info.codec_config.span());
  if (!parsed.ok()) {
    return parsed.status();
  }
  return std::move(parsed->config_obus);
}

}
