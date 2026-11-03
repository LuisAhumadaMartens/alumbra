#ifndef UMBRA_DEMUX_TS_FRAMING_H_
#define UMBRA_DEMUX_TS_FRAMING_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/demux/caption_extractor.h"

namespace umbra::demux::ts {

inline constexpr penumbra::Rational kTimeBase =
    penumbra::Rational::Of(1, 90000);

enum class EsKind : uint8_t {
  kH264,
  kHevc,
  kAdts,
  kMpegAudio,
  kAc3,
  kEac3,
  kId3,
};

struct VideoScan {
  bool key_frame = false;
  bool has_sei = false;
  bool has_parameter_sets = false;
};

[[nodiscard]] VideoScan ScanVideo(CaptionCodec codec,
                                  std::span<const uint8_t> payload);

[[nodiscard]] absl::StatusOr<penumbra::StreamInfo> BuildVideoInfo(
    CaptionCodec codec, std::span<const uint8_t> payload, uint32_t track_id,
    const std::string& language);

struct AudioFrame {
  size_t offset = 0;
  size_t size = 0;
  uint32_t samples = 0;
  int32_t sample_rate = 0;
  bool dependent = false;
  uint8_t dependents = 0;
};

struct AudioSplit {
  std::vector<AudioFrame> frames;
  bool lost_sync = false;
};

[[nodiscard]] AudioSplit SplitAudio(EsKind kind,
                                    std::span<const uint8_t> payload);

[[nodiscard]] std::optional<AudioFrame> ParseAudioFrame(
    EsKind kind, std::span<const uint8_t> payload);

[[nodiscard]] absl::StatusOr<penumbra::StreamInfo> BuildAudioInfo(
    EsKind kind, std::span<const uint8_t> frame, uint32_t track_id,
    const std::string& language);

}

#endif
