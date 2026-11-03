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

}
