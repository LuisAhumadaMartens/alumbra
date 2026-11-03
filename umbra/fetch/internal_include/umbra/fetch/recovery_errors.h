#ifndef UMBRA_FETCH_RECOVERY_ERRORS_H_
#define UMBRA_FETCH_RECOVERY_ERRORS_H_

#include <memory>
#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "umbra/base/url_redactor.h"
#include "umbra/fetch/backoff.h"
#include "umbra/fetch/fetch_failure.h"

namespace umbra::fetch {

[[nodiscard]] absl::Status MakeRecoveryExhaustedError(
    const base::UrlRedactor& redactor, const absl::Status& cause,
    const std::optional<FetchFailure>& failure, int attempts);

[[nodiscard]] absl::Status MakeManifestLoadError(
    const base::UrlRedactor& redactor, const std::string& url,
    const absl::Status& cause, int http_status, int attempts);

struct ManifestRetryDecision {
  bool retry = false;
  absl::Duration delay;
  absl::Status error;
};

class ManifestLoadRecovery {
 public:
  ManifestLoadRecovery(BackoffPolicy policy,
                       std::shared_ptr<JitterSource> jitter,
                       std::shared_ptr<const base::UrlRedactor> redactor);

  [[nodiscard]] ManifestRetryDecision OnFailure(
      const std::string& url, const absl::Status& status, int http_status,
      std::optional<absl::Duration> retry_after);
  void Reset();

 private:
  BackoffPolicy policy_;
  std::shared_ptr<JitterSource> jitter_;
  std::shared_ptr<const base::UrlRedactor> redactor_;
  int attempts_ = 0;
};

}

#endif
