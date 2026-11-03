#include "umbra/fetch/recovery_errors.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/status.h"
#include "umbra/base/url_redactor.h"
#include "umbra/fetch/backoff.h"
#include "umbra/fetch/error_classifier.h"
#include "umbra/fetch/fetch_failure.h"

namespace umbra::fetch {

namespace {

base::ErrorCode CodeFor(const absl::Status& cause) {
  if (cause.code() == absl::StatusCode::kPermissionDenied ||
      cause.code() == absl::StatusCode::kUnauthenticated) {
    return base::ErrorCode::kNetworkAccessDenied;
  }
  return base::ErrorCode::kNetworkRequestFailed;
}

}

absl::Status MakeRecoveryExhaustedError(
    const base::UrlRedactor& redactor, const absl::Status& cause,
    const std::optional<FetchFailure>& failure, int attempts) {
  std::map<std::string, std::string> context;
  context["cause"] = absl::StatusCodeToString(cause.code());
  context["attempts"] = absl::StrCat(attempts);
  if (failure.has_value()) {
    context["url"] = failure->url;
    context["http_status"] = absl::StrCat(failure->http_status);
  }
  return base::MakeError(CodeFor(cause), redactor, std::move(context));
}

absl::Status MakeManifestLoadError(const base::UrlRedactor& redactor,
                                   const std::string& url,
                                   const absl::Status& cause, int http_status,
                                   int attempts) {
  std::map<std::string, std::string> context;
  context["cause"] = absl::StatusCodeToString(cause.code());
  context["attempts"] = absl::StrCat(attempts);
  context["url"] = url;
  context["http_status"] = absl::StrCat(http_status);
  return base::MakeError(CodeFor(cause), redactor, std::move(context));
}

ManifestLoadRecovery::ManifestLoadRecovery(
    BackoffPolicy policy, std::shared_ptr<JitterSource> jitter,
    std::shared_ptr<const base::UrlRedactor> redactor)
    : policy_(policy),
      jitter_(std::move(jitter)),
      redactor_(std::move(redactor)) {}

void ManifestLoadRecovery::Reset() { attempts_ = 0; }

ManifestRetryDecision ManifestLoadRecovery::OnFailure(
    const std::string& url, const absl::Status& status, int http_status,
    std::optional<absl::Duration> retry_after) {
  FetchFailure failure;
  failure.url = url;
  failure.http_status = http_status;
  failure.retry_after = retry_after;
  const Classification classification =
      ClassifyFailure(status, failure, LivePosition::kNotLive, policy_);
  ManifestRetryDecision decision;
  const bool transient = classification.action == RecoveryAction::kRetry;
  if (transient && attempts_ < policy_.max_attempts) {
    decision.retry = true;
    decision.delay = classification.retry_delay.has_value()
                         ? *classification.retry_delay
                         : BackoffDelay(policy_, attempts_, *jitter_);
    ++attempts_;
    return decision;
  }
  decision.error =
      MakeManifestLoadError(*redactor_, url, status, http_status, attempts_);
  return decision;
}

}
