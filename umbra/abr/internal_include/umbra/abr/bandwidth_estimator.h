#ifndef UMBRA_ABR_BANDWIDTH_ESTIMATOR_H_
#define UMBRA_ABR_BANDWIDTH_ESTIMATOR_H_

#include <cstdint>
#include <memory>
#include <optional>

#include "absl/time/time.h"

namespace umbra::abr {

struct BandwidthEstimatorConfig {
  std::int64_t default_bandwidth_bps = 1000000;
  std::int64_t fast_half_life_ms = 2000;
  std::int64_t slow_half_life_ms = 5000;
  std::uint64_t min_sample_bytes = 16384;
};

struct BandwidthEstimate {
  std::int64_t bandwidth_bps;
  std::int64_t ttfb_ms;
  std::uint64_t sample_count;
  bool has_samples;
};

class BandwidthEstimator {
 public:
  virtual ~BandwidthEstimator() = default;
  virtual void AddSample(std::uint64_t bytes, absl::Duration ttfb,
                         absl::Duration transfer_time, bool cache_hit) = 0;
  virtual void AddChunkSample(std::uint64_t bytes, absl::Duration ttfb,
                              absl::Duration active_transfer_time,
                              bool cache_hit) = 0;
  void AddSample(std::uint64_t bytes, absl::Duration ttfb,
                 absl::Duration transfer_time) {
    AddSample(bytes, ttfb, transfer_time, false);
  }
  void AddChunkSample(std::uint64_t bytes, absl::Duration ttfb,
                      absl::Duration active_transfer_time) {
    AddChunkSample(bytes, ttfb, active_transfer_time, false);
  }
  [[nodiscard]] virtual BandwidthEstimate GetEstimate() const = 0;
};

std::unique_ptr<BandwidthEstimator> CreateBandwidthEstimator(
    BandwidthEstimatorConfig config = {});

}

#endif
