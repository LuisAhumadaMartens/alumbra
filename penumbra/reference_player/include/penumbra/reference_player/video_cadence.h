#ifndef PENUMBRA_REFERENCE_PLAYER_VIDEO_CADENCE_H_
#define PENUMBRA_REFERENCE_PLAYER_VIDEO_CADENCE_H_

#include <cstdint>
#include <vector>

#include "penumbra/interface/media_time.h"
#include "penumbra/interface/rational.h"

namespace penumbra {

class VideoCadenceEstimator {
 public:
  explicit VideoCadenceEstimator(
      TimeDelta minimum_stable_time = TimeDelta::FromSeconds(2));

  void Observe(Rational frame_rate, Rational refresh_rate,
               int64_t monotonic_us);
  void Reset();
  [[nodiscard]] const std::vector<uint32_t>& Cadence() const;
  [[nodiscard]] bool Stable() const;
  [[nodiscard]] uint64_t CadenceBreaks() const;

 private:
  [[nodiscard]] static std::vector<uint32_t> Estimate(Rational frame_rate,
                                                      Rational refresh_rate);

  TimeDelta minimum_stable_time_;
  std::vector<uint32_t> cadence_;
  std::vector<uint32_t> candidate_;
  int64_t candidate_since_us_ = 0;
  bool has_candidate_ = false;
  bool stable_ = false;
  uint64_t cadence_breaks_ = 0;
};

}

#endif
