#include "umbra/abr/bandwidth_estimator.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>

#include "absl/time/time.h"
#include "umbra/observability/metrics.h"

namespace umbra::abr {
namespace {

double HalfLifeAlpha(absl::Duration elapsed, std::int64_t half_life_ms) {
  const double elapsed_ms = absl::ToDoubleMilliseconds(elapsed);
  return 1.0 - std::exp2(-elapsed_ms / static_cast<double>(half_life_ms));
}

std::int64_t ClampToInt64(double value) {
  const auto maximum =
      static_cast<double>(std::numeric_limits<std::int64_t>::max());
  return static_cast<std::int64_t>(std::clamp(value, 0.0, maximum));
}

class DualEwmaBandwidthEstimator final : public BandwidthEstimator {
 public:
  explicit DualEwmaBandwidthEstimator(BandwidthEstimatorConfig config)
      : config_(config),
        fast_bps_(static_cast<double>(config.default_bandwidth_bps)),
        slow_bps_(static_cast<double>(config.default_bandwidth_bps)) {}

  void AddSample(std::uint64_t bytes, absl::Duration ttfb,
                 absl::Duration transfer_time, bool cache_hit) override {
    Add(bytes, ttfb, transfer_time, cache_hit);
  }

  void AddChunkSample(std::uint64_t bytes, absl::Duration ttfb,
                      absl::Duration active_transfer_time,
                      bool cache_hit) override {
    Add(bytes, ttfb, active_transfer_time, cache_hit);
  }

  [[nodiscard]] BandwidthEstimate GetEstimate() const override {
    const double bandwidth =
        sample_count_ == 0 ? static_cast<double>(config_.default_bandwidth_bps)
                           : std::min(fast_bps_, slow_bps_);
    return {.bandwidth_bps = ClampToInt64(bandwidth),
            .ttfb_ms = sample_count_ == 0 ? 0 : ClampToInt64(ttfb_ms_),
            .sample_count = sample_count_,
            .has_samples = sample_count_ != 0};
  }

 private:
  void Add(std::uint64_t bytes, absl::Duration ttfb,
           absl::Duration transfer_time, bool cache_hit) {
    if (cache_hit || bytes < config_.min_sample_bytes ||
        transfer_time <= absl::ZeroDuration() || ttfb < absl::ZeroDuration()) {
      return;
    }
    const double seconds = absl::ToDoubleSeconds(transfer_time);
    const double sample_bps =
        std::min(static_cast<double>(bytes) * 8.0 / seconds,
                 static_cast<double>(std::numeric_limits<std::int64_t>::max()));
    if (sample_count_ == 0) {
      fast_bps_ = sample_bps;
      slow_bps_ = sample_bps;
      ttfb_ms_ = std::min(
          absl::ToDoubleMilliseconds(ttfb),
          static_cast<double>(std::numeric_limits<std::int64_t>::max()));
    } else {
      fast_bps_ += HalfLifeAlpha(transfer_time, config_.fast_half_life_ms) *
                   (sample_bps - fast_bps_);
      slow_bps_ += HalfLifeAlpha(transfer_time, config_.slow_half_life_ms) *
                   (sample_bps - slow_bps_);
      const double ttfb_alpha = HalfLifeAlpha(ttfb, config_.slow_half_life_ms);
      const double sample_ttfb_ms = std::min(
          absl::ToDoubleMilliseconds(ttfb),
          static_cast<double>(std::numeric_limits<std::int64_t>::max()));
      ttfb_ms_ += ttfb_alpha * (sample_ttfb_ms - ttfb_ms_);
    }
    ++sample_count_;
    observability::ReportMetric(observability::BandwidthEstimateMetric{
        .bandwidth_bps = GetEstimate().bandwidth_bps,
        .ttfb_ms = GetEstimate().ttfb_ms,
        .sample_count = sample_count_});
  }

  BandwidthEstimatorConfig config_;
  double fast_bps_;
  double slow_bps_;
  double ttfb_ms_ = 0.0;
  std::uint64_t sample_count_ = 0;
};

}

std::unique_ptr<BandwidthEstimator> CreateBandwidthEstimator(
    BandwidthEstimatorConfig config) {
  return std::make_unique<DualEwmaBandwidthEstimator>(config);
}

}
