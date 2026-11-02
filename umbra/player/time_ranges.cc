#include "umbra/player/time_ranges.h"

#include <cstddef>
#include <limits>

namespace umbra::player {

double TimeRanges::start(size_t index) const {
  if (index >= ranges_.size()) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  return ranges_[index].start;
}

double TimeRanges::end(size_t index) const {
  if (index >= ranges_.size()) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  return ranges_[index].end;
}

}
