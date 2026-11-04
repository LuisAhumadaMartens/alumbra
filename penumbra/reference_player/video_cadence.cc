#include "penumbra/reference_player/video_cadence.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "penumbra/interface/media_time.h"
#include "penumbra/interface/rational.h"

namespace penumbra {
namespace {

constexpr double kMaximumAverageDrift = 0.05;

double Value(Rational value) {
  if (!value.is_positive()) {
    return 0.0;
  }
  return static_cast<double>(value.numerator()) /
         static_cast<double>(value.denominator());
}

std::vector<uint32_t> BuildPattern(uint32_t frames, int64_t refreshes) {
  std::vector<uint32_t> pattern;
  pattern.reserve(frames);
  int64_t previous = 0;
  for (uint32_t index = 1; index <= frames; ++index) {
    const auto next = static_cast<int64_t>(std::floor(
        (static_cast<double>(refreshes) * index / frames) + 0.5 - 1e-9));
    if (next <= previous) {
      return {};
    }
    pattern.push_back(static_cast<uint32_t>(next - previous));
    previous = next;
  }
  for (size_t period = 1; period < pattern.size(); ++period) {
    if (pattern.size() % period != 0) {
      continue;
    }
    bool repeats = true;
    for (size_t index = period; index < pattern.size(); ++index) {
      repeats = repeats && pattern[index] == pattern[index % period];
    }
    if (repeats) {
      pattern.resize(period);
      break;
    }
  }
  return pattern;
}

}

VideoCadenceEstimator::VideoCadenceEstimator(TimeDelta minimum_stable_time)
    : minimum_stable_time_(minimum_stable_time) {}

std::vector<uint32_t> VideoCadenceEstimator::Estimate(Rational frame_rate,
                                                      Rational refresh_rate) {
  const double ratio = Value(refresh_rate) / Value(frame_rate);
  if (!std::isfinite(ratio) || ratio <= 0.0 || ratio > 1000.0) {
    return {};
  }
  std::vector<uint32_t> best;
  double best_drift = std::numeric_limits<double>::infinity();
  for (uint32_t frames = 1; frames <= 5; ++frames) {
    const double exact = ratio * frames;
    const auto refreshes = static_cast<int64_t>(std::llround(exact));
    const double drift =
        std::abs(static_cast<double>(refreshes) - exact) / frames;
    if (refreshes <= 0 || drift > kMaximumAverageDrift) {
      continue;
    }
    std::vector<uint32_t> candidate = BuildPattern(frames, refreshes);
    if (candidate.empty()) {
      continue;
    }
    if (drift < best_drift - 1e-9 || (std::abs(drift - best_drift) <= 1e-9 &&
                                      candidate.size() > best.size())) {
      best = std::move(candidate);
      best_drift = drift;
    }
  }
  return best;
}

void VideoCadenceEstimator::Observe(Rational frame_rate, Rational refresh_rate,
                                    int64_t monotonic_us) {
  const std::vector<uint32_t> estimate = Estimate(frame_rate, refresh_rate);
  if (estimate == cadence_) {
    candidate_.clear();
    has_candidate_ = false;
    stable_ = !cadence_.empty();
    return;
  }
  if (!has_candidate_ || estimate != candidate_) {
    candidate_ = estimate;
    candidate_since_us_ = monotonic_us;
    has_candidate_ = true;
    stable_ = false;
    return;
  }
  if (monotonic_us - candidate_since_us_ <
      minimum_stable_time_.InMicroseconds()) {
    stable_ = false;
    return;
  }
  if (!cadence_.empty() && cadence_ != candidate_) {
    ++cadence_breaks_;
  }
  cadence_ = candidate_;
  candidate_.clear();
  has_candidate_ = false;
  stable_ = !cadence_.empty();
}

void VideoCadenceEstimator::Reset() {
  cadence_.clear();
  candidate_.clear();
  candidate_since_us_ = 0;
  has_candidate_ = false;
  stable_ = false;
}

const std::vector<uint32_t>& VideoCadenceEstimator::Cadence() const {
  return cadence_;
}

bool VideoCadenceEstimator::Stable() const { return stable_; }

uint64_t VideoCadenceEstimator::CadenceBreaks() const {
  return cadence_breaks_;
}

}
