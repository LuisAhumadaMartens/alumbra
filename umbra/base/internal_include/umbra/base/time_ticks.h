#ifndef UMBRA_BASE_TIME_TICKS_H_
#define UMBRA_BASE_TIME_TICKS_H_

#include "absl/time/time.h"

namespace umbra::base {

class TimeTicks {
 public:
  TimeTicks() = default;

  static TimeTicks FromSinceOrigin(absl::Duration since_origin) {
    return TimeTicks(since_origin);
  }

  absl::Duration SinceOrigin() const { return since_origin_; }

  friend TimeTicks operator+(TimeTicks ticks, absl::Duration delta) {
    return TimeTicks(ticks.since_origin_ + delta);
  }
  friend absl::Duration operator-(TimeTicks lhs, TimeTicks rhs) {
    return lhs.since_origin_ - rhs.since_origin_;
  }
  friend bool operator==(TimeTicks lhs, TimeTicks rhs) {
    return lhs.since_origin_ == rhs.since_origin_;
  }
  friend bool operator<(TimeTicks lhs, TimeTicks rhs) {
    return lhs.since_origin_ < rhs.since_origin_;
  }
  friend bool operator>(TimeTicks lhs, TimeTicks rhs) { return rhs < lhs; }
  friend bool operator<=(TimeTicks lhs, TimeTicks rhs) { return !(rhs < lhs); }
  friend bool operator>=(TimeTicks lhs, TimeTicks rhs) { return !(lhs < rhs); }

 private:
  explicit TimeTicks(absl::Duration since_origin)
      : since_origin_(since_origin) {}

  absl::Duration since_origin_;
};

}

#endif
