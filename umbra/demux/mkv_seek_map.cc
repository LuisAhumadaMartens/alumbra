#include "umbra/demux/mkv_seek_map.h"

#include <algorithm>
#include <functional>
#include <optional>

#include "penumbra/interface/media_time.h"
#include "umbra/demux/demuxer.h"

namespace umbra::demux {

std::optional<SeekPoint> MatroskaSeekMap::SeekPointFor(
    penumbra::MediaTime target) const {
  if (points_.empty() || !target.is_finite()) {
    return std::nullopt;
  }
  const penumbra::MediaTime adjusted =
      pre_roll_.is_finite() ? target - pre_roll_ : target;
  const auto found = std::ranges::upper_bound(points_, adjusted, std::less<>(),
                                              &SeekPoint::time);
  if (found == points_.begin()) {
    return points_.front();
  }
  return *(found - 1);
}

std::optional<SeekPoint> MatroskaSeekMap::NearestSeekPointFor(
    penumbra::MediaTime target) const {
  if (points_.empty() || !target.is_finite()) {
    return std::nullopt;
  }
  const auto found = std::ranges::lower_bound(points_, target, std::less<>(),
                                              &SeekPoint::time);
  if (found == points_.begin()) {
    return points_.front();
  }
  if (found == points_.end()) {
    return points_.back();
  }
  const SeekPoint& before = *(found - 1);
  const SeekPoint& after = *found;
  return (target - before.time) <= (after.time - target) ? before : after;
}

}
