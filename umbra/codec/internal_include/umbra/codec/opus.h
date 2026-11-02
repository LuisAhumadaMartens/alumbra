#ifndef UMBRA_CODEC_OPUS_H_
#define UMBRA_CODEC_OPUS_H_

#include <cstdint>
#include <vector>

#include "absl/status/statusor.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/codec/codec_string.h"
#include "umbra/codec/reader_util.h"

namespace umbra::codec {

inline constexpr std::int32_t kOpusOutputSampleRate = 48000;

struct OpusConfig {
  std::uint8_t channel_count = 2;
  std::uint16_t pre_skip = 0;
  std::uint32_t input_sample_rate = 0;
  std::int16_t output_gain = 0;
  std::uint8_t mapping_family = 0;
  std::uint8_t stream_count = 1;
  std::uint8_t coupled_count = 0;
  std::vector<std::uint8_t> channel_mapping;

  friend bool operator==(const OpusConfig&, const OpusConfig&) = default;
};

[[nodiscard]] absl::StatusOr<OpusConfig> ParseOpusHead(Bytes data);

[[nodiscard]] absl::StatusOr<OpusConfig> ParseDOps(Bytes data);

[[nodiscard]] absl::StatusOr<std::vector<std::uint8_t>> SerializeDOps(
    const OpusConfig& config);

[[nodiscard]] OpusCodec ToCodec(const OpusConfig& config);

[[nodiscard]] penumbra::AudioInfo ToAudioInfo(const OpusConfig& config);

}

#endif
