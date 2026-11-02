#include "umbra/demux/mp4_seek_map.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>

#include "penumbra/interface/media_time.h"
#include "penumbra/interface/rational.h"
#include "umbra/demux/demuxer.h"
#include "umbra/demux/mp4_types.h"

namespace umbra::demux {

size_t FindSeekIndex(const SampleList& samples, int64_t shift,
                     int64_t target_ticks) {
  if (samples.empty()) {
    return 0;
  }
  size_t low = 0;
  size_t high = samples.size();
  while (low < high) {
    const size_t middle = low + ((high - low) / 2);
    if (samples[middle].dts + shift <= target_ticks) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }
  size_t index = low == 0 ? 0 : low - 1;
  while (index > 0 && (!samples[index].sync ||
                       samples[index].dts + samples[index].cts_offset + shift >
                           target_ticks)) {
    --index;
  }
  return index;
}

std::optional<SeekPoint> ProgressiveSeekMap::SeekPointFor(
    penumbra::MediaTime target) const {
  if (!target.is_finite()) {
    return std::nullopt;
  }
  uint64_t offset = std::numeric_limits<uint64_t>::max();
  std::optional<penumbra::MediaTime> time;
  for (const SeekTrack& track : tracks_) {
    if (track.samples->empty()) {
      continue;
    }
    const std::optional<int64_t> ticks = penumbra::Rescale(
        target.InMicroseconds(), penumbra::kMicrosecondTimeBase,
        track.time_base, penumbra::Rounding::kDown);
    if (!ticks.has_value()) {
      continue;
    }
    const size_t index = FindSeekIndex(*track.samples, track.shift, *ticks);
    const SampleRef& sample = (*track.samples)[index];
    offset = std::min(offset, sample.offset);
    if (track.reference || !time.has_value()) {
      const penumbra::Timestamp pts{
          .value = sample.dts + sample.cts_offset + track.shift,
          .time_base = track.time_base};
      time = penumbra::ToMediaTime(pts, penumbra::Rounding::kUp);
    }
  }
  if (!time.has_value()) {
    return std::nullopt;
  }
  return SeekPoint{.time = *time, .byte_offset = offset};
}

std::optional<SeekPoint> IndexedSeekMap::SeekPointFor(
    penumbra::MediaTime target) const {
  if (points_.empty()) {
    return std::nullopt;
  }
  const auto found = std::ranges::upper_bound(points_, target, std::less<>(),
                                              &SeekPoint::time);
  if (found == points_.begin()) {
    return points_.front();
  }
  return *(found - 1);
}

}
