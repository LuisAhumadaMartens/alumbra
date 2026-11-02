#ifndef UMBRA_CODEC_AAC_H_
#define UMBRA_CODEC_AAC_H_

#include <cstdint>

#include "absl/status/statusor.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/codec/codec_string.h"
#include "umbra/codec/reader_util.h"

namespace umbra::codec {

inline constexpr std::uint8_t kAacLc = 2;
inline constexpr std::uint8_t kAacSbr = 5;
inline constexpr std::uint8_t kAacPs = 29;

struct AacConfig {
  std::uint8_t audio_object_type = kAacLc;
  std::int32_t sampling_frequency = 0;
  std::uint8_t channel_configuration = 0;
  std::int32_t channel_count = 0;
  std::uint64_t channel_mask = 0;
  bool frame_length_960 = false;
  bool sbr = false;
  bool ps = false;
  bool explicit_extension = false;
  std::int32_t extension_sampling_frequency = 0;

  [[nodiscard]] std::uint8_t OutputObjectType() const {
    if (ps) {
      return kAacPs;
    }
    return sbr ? kAacSbr : audio_object_type;
  }
  [[nodiscard]] std::int32_t OutputSampleRate() const {
    return sbr ? extension_sampling_frequency : sampling_frequency;
  }
  [[nodiscard]] std::int32_t OutputChannelCount() const {
    return ps ? 2 : channel_count;
  }
};

[[nodiscard]] absl::StatusOr<AacConfig> ParseAudioSpecificConfig(Bytes data);

[[nodiscard]] Mp4aCodec ToCodec(const AacConfig& config);

[[nodiscard]] penumbra::AudioInfo ToAudioInfo(const AacConfig& config);

}

#endif
