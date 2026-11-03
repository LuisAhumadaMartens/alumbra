#ifndef UMBRA_DEMUX_MKV_TRACKS_H_
#define UMBRA_DEMUX_MKV_TRACKS_H_

#include <cstdint>
#include <optional>
#include <vector>

#include "absl/status/statusor.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/demux/ebml.h"
#include "umbra/demux/mkv_elements.h"

namespace umbra::demux {

struct MkvTrackConfig {
  uint64_t number = 0;
  penumbra::StreamInfo stream;
  std::optional<int64_t> default_duration_ns;
  int64_t codec_delay_ticks = 0;
  int64_t seek_pre_roll_micros = 0;
  std::vector<uint8_t> header_strip;
  bool opus = false;
};

[[nodiscard]] absl::StatusOr<std::optional<MkvTrackConfig>> BuildTrackConfig(
    const MkvTrack& track, penumbra::Rational scale_base);

[[nodiscard]] std::optional<int64_t> OpusPacketTicks(EbmlBytes packet);

}

#endif
