#include "umbra/fetch/backoff.h"

#include <algorithm>
#include <cstdint>

#include "absl/status/status.h"
#include "absl/time/time.h"

namespace umbra::fetch {

namespace {

constexpr int kMaxDoublings = 30;
constexpr int kUnitBits = 53;
constexpr double kUnitScale = 1.0 / 9007199254740992.0;

}

absl::Status ValidateBackoffPolicy(const BackoffPolicy& policy) {
  if (policy.max_attempts < 0) {
    return absl::InvalidArgumentError("max_attempts must not be negative");
  }
  if (policy.initial_backoff < absl::ZeroDuration()) {
    return absl::InvalidArgumentError("initial_backoff must not be negative");
  }
  if (policy.max_backoff < policy.initial_backoff) {
    return absl::InvalidArgumentError(
        "max_backoff must not be shorter than initial_backoff");
  }
  return absl::OkStatus();
}

double SeededJitter::NextUnit() {
  const uint64_t bits = engine_() >> (64 - kUnitBits);
  return static_cast<double>(bits) * kUnitScale;
}

absl::Duration BackoffCeiling(const BackoffPolicy& policy, int attempt) {
  const int doublings = std::clamp(attempt, 0, kMaxDoublings);
  const absl::Duration scaled =
      policy.initial_backoff * (int64_t{1} << doublings);
  return std::min(scaled, policy.max_backoff);
}

absl::Duration BackoffDelay(const BackoffPolicy& policy, int attempt,
                            JitterSource& jitter) {
  const absl::Duration ceiling = BackoffCeiling(policy, attempt);
  const double unit = std::clamp(jitter.NextUnit(), 0.0, 1.0);
  return absl::Milliseconds(
      absl::ToInt64Milliseconds((ceiling * 0.5) + (ceiling * (0.5 * unit))));
}

}
