#ifndef UMBRA_PLAYER_TIME_RANGES_H_
#define UMBRA_PLAYER_TIME_RANGES_H_

#include <cstddef>
#include <utility>
#include <vector>

namespace umbra::player {

struct TimeRange {
  double start = 0.0;
  double end = 0.0;
};

class TimeRanges {
 public:
  TimeRanges() = default;
  explicit TimeRanges(std::vector<TimeRange> ranges)
      : ranges_(std::move(ranges)) {}

  [[nodiscard]] size_t length() const { return ranges_.size(); }
  [[nodiscard]] double start(size_t index) const;
  [[nodiscard]] double end(size_t index) const;

 private:
  std::vector<TimeRange> ranges_;
};

}

#endif
