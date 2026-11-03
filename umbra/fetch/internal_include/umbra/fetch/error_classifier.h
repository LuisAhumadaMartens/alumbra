#ifndef UMBRA_FETCH_ERROR_CLASSIFIER_H_
#define UMBRA_FETCH_ERROR_CLASSIFIER_H_

#include <cstdint>
#include <optional>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "umbra/fetch/backoff.h"
#include "umbra/fetch/fetch_failure.h"

namespace umbra::fetch {

enum class LivePosition : uint8_t { kNotLive, kNearEdge, kBehindWindow };

enum class RecoveryAction : uint8_t { kRetry, kFailover, kResync, kFatal };

struct Classification {
  RecoveryAction action = RecoveryAction::kFatal;
  std::optional<absl::Duration> retry_delay;
  std::optional<absl::Duration> pathway_hold;
  bool pathway_wide = true;
  bool allows_rendition_step = true;
};

[[nodiscard]] Classification ClassifyFailure(
    const absl::Status& status, const std::optional<FetchFailure>& failure,
    LivePosition live, const BackoffPolicy& policy);

}

#endif
