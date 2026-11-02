#include "umbra/codec/flac.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/base/status.h"
#include "umbra/codec/codec_string.h"
#include "umbra/codec/reader_util.h"

namespace umbra::codec {
namespace {

constexpr std::uint16_t kMinBlockSize = 16;
constexpr std::uint8_t kMinBitsPerSample = 4;
constexpr std::uint32_t kMaxSampleRate = 655350;
constexpr std::uint8_t kStreamInfoType = 0;

constexpr std::array<std::uint64_t, 9> kChannelMasks{
    0, 0x4, 0x3, 0x7, 0x33, 0x37, 0x3f, 0x70f, 0x63f,
};

absl::Status Validate(const FlacStreamInfo& info) {
  if (info.min_block_size < kMinBlockSize ||
      info.max_block_size < info.min_block_size || info.sample_rate == 0 ||
      info.sample_rate > kMaxSampleRate ||
      info.bits_per_sample < kMinBitsPerSample) {
    return Corrupt("FLAC STREAMINFO field is out of range");
  }
  return absl::OkStatus();
}

penumbra::SampleFormat FormatFor(std::uint8_t bits) {
  return bits <= 16 ? penumbra::SampleFormat::kS16
                    : penumbra::SampleFormat::kS32;
}

}

absl::StatusOr<FlacStreamInfo> ParseFlacStreamInfo(Bytes data) {
  if (data.size() < kFlacStreamInfoSize) {
    return Corrupt("FLAC STREAMINFO is shorter than 34 bytes");
  }
  RecordReader reader(data.first(kFlacStreamInfoSize), "FLAC STREAMINFO");
  FlacStreamInfo info;
  ALUMBRA_ASSIGN_OR_RETURN(info.min_block_size, reader.U16());
  ALUMBRA_ASSIGN_OR_RETURN(info.max_block_size, reader.U16());
  ALUMBRA_ASSIGN_OR_RETURN(info.min_frame_size, reader.U24());
  ALUMBRA_ASSIGN_OR_RETURN(info.max_frame_size, reader.U24());
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint64_t packed, reader.Unsigned(8));
  info.sample_rate = static_cast<std::uint32_t>(packed >> 44U);
  info.channel_count = static_cast<std::uint8_t>(((packed >> 41U) & 7U) + 1);
  info.bits_per_sample = static_cast<std::uint8_t>(((packed >> 36U) & 31U) + 1);
  info.total_samples = packed & 0xfffffffffULL;
  ALUMBRA_ASSIGN_OR_RETURN(const Bytes digest, reader.Take(info.md5.size()));
  std::ranges::copy(digest, info.md5.begin());
  ALUMBRA_RETURN_IF_ERROR(Validate(info));
  return info;
}

absl::StatusOr<FlacStreamInfo> ParseDfLa(Bytes full_box_payload) {
  RecordReader reader(full_box_payload, "dfLa");
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint32_t header, reader.U32());
  if ((header >> 24U) != 0) {
    return Unsupported("dfLa version");
  }
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint8_t block_header, reader.U8());
  if ((block_header & 0x7fU) != kStreamInfoType) {
    return Corrupt("dfLa does not begin with STREAMINFO");
  }
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint32_t length, reader.U24());
  if (length != kFlacStreamInfoSize) {
    return Corrupt("dfLa STREAMINFO has the wrong length");
  }
  ALUMBRA_ASSIGN_OR_RETURN(const Bytes block, reader.Take(length));
  return ParseFlacStreamInfo(block);
}

FlacCodec ToCodec([[maybe_unused]] const FlacStreamInfo& info) { return {}; }

penumbra::AudioInfo ToAudioInfo(const FlacStreamInfo& info) {
  penumbra::AudioInfo audio;
  audio.sample_rate = static_cast<std::int32_t>(info.sample_rate);
  audio.channel_count = info.channel_count;
  if (info.channel_count < kChannelMasks.size()) {
    audio.channel_mask = kChannelMasks.at(info.channel_count);
  }
  audio.sample_format = FormatFor(info.bits_per_sample);
  return audio;
}

}
