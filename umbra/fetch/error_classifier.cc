#include "umbra/fetch/error_classifier.h"

#include <optional>

#include "absl/status/status.h"
#include "umbra/fetch/backoff.h"
#include "umbra/fetch/fetch_failure.h"

namespace umbra::fetch {

namespace {

Classification Retry(const std::optional<FetchFailure>& failure,
                     const BackoffPolicy& policy) {
  Classification result;
  result.action = RecoveryAction::kRetry;
  if (failure.has_value() && failure->retry_after.has_value()) {
    if (*failure->retry_after <= policy.max_backoff) {
      result.retry_delay = failure->retry_after;
    } else {
      result.action = RecoveryAction::kFailover;
      result.pathway_hold = failure->retry_after;
    }
  }
  return result;
}

Classification Failover(bool pathway_wide, bool allows_rendition_step) {
  Classification result;
  result.action = RecoveryAction::kFailover;
  result.pathway_wide = pathway_wide;
  result.allows_rendition_step = allows_rendition_step;
  return result;
}

}

Classification ClassifyFailure(const absl::Status& status,
                               const std::optional<FetchFailure>& failure,
                               LivePosition live, const BackoffPolicy& policy) {
  switch (status.code()) {
    case absl::StatusCode::kUnavailable:
    case absl::StatusCode::kDeadlineExceeded:
    case absl::StatusCode::kDataLoss:
    case absl::StatusCode::kAborted:
    case absl::StatusCode::kResourceExhausted:
      return Retry(failure, policy);
    case absl::StatusCode::kNotFound:
      if (live == LivePosition::kBehindWindow) {
        Classification result;
        result.action = RecoveryAction::kResync;
        return result;
      }
      if (live == LivePosition::kNearEdge) {
        return Retry(std::nullopt, policy);
      }
      return Failover(false, true);
    case absl::StatusCode::kPermissionDenied:
    case absl::StatusCode::kUnauthenticated:
      return Failover(true, false);
    case absl::StatusCode::kInvalidArgument:
    case absl::StatusCode::kOutOfRange:
      return Failover(false, true);
    default:
      return Classification{};
  }
}

}
