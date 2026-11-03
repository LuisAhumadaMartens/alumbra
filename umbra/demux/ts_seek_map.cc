#include "umbra/demux/ts_seek_map.h"

#include <algorithm>
#include <cstdint>
#include <optional>

#include "penumbra/interface/media_time.h"
#include "umbra/demux/demuxer.h"

namespace umbra::demux {

penumbra::TimeDelta TsSeekMap::Duration() const {
  if (!seekable_) {
    return penumbra::TimeDelta::Unknown();
  }
  return end_ - start_;
}

std::optional<SeekPoint> TsSeekMap::SeekPointFor(
    penumbra::MediaTime target) const {
  if (!seekable_ || !target.is_finite()) {
    return std::nullopt;
  }
  const penumbra::MediaTime time = std::clamp(target, start_, end_);
  const auto span = static_cast<double>((end_ - start_).InMicroseconds());
  const auto elapsed = static_cast<double>((time - start_).InMicroseconds());
  const double ratio = span > 0 ? elapsed / span : 0.0;
  const auto bytes = static_cast<double>(size_ - start_offset_);
  return SeekPoint{
      .time = time,
      .byte_offset = start_offset_ + static_cast<uint64_t>(ratio * bytes)};
}

}
