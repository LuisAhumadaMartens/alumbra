#ifndef UMBRA_CODEC_FLAC_H_
#define UMBRA_CODEC_FLAC_H_

#include <array>
#include <cstddef>
#include <cstdint>

#include "absl/status/statusor.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/codec/codec_string.h"
#include "umbra/codec/reader_util.h"

namespace umbra::codec {

inline constexpr std::size_t kFlacStreamInfoSize = 34;

struct FlacStreamInfo {
  std::uint16_t min_block_size = 0;
  std::uint16_t max_block_size = 0;
  std::uint32_t min_frame_size = 0;
  std::uint32_t max_frame_size = 0;
  std::uint32_t sample_rate = 0;
  std::uint8_t channel_count = 0;
  std::uint8_t bits_per_sample = 0;
  std::uint64_t total_samples = 0;
  std::array<std::uint8_t, 16> md5{};

  friend bool operator==(const FlacStreamInfo&,
                         const FlacStreamInfo&) = default;
};

[[nodiscard]] absl::StatusOr<FlacStreamInfo> ParseFlacStreamInfo(Bytes data);

[[nodiscard]] absl::StatusOr<FlacStreamInfo> ParseDfLa(Bytes full_box_payload);

[[nodiscard]] FlacCodec ToCodec(const FlacStreamInfo& info);

[[nodiscard]] penumbra::AudioInfo ToAudioInfo(const FlacStreamInfo& info);

}

#endif
