#ifndef UMBRA_FETCH_FETCH_FAILURE_H_
#define UMBRA_FETCH_FETCH_FAILURE_H_

#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "penumbra/interface/http_transport.h"

namespace umbra::fetch {

struct FetchFailure {
  std::string url;
  penumbra::RequestClass request_class = penumbra::RequestClass::kMedia;
  int http_status = 0;
  std::optional<absl::Duration> retry_after;
};

[[nodiscard]] std::optional<absl::Duration> ParseRetryAfter(
    const std::vector<penumbra::HttpHeader>& headers);

[[nodiscard]] absl::Status AttachFetchFailure(absl::Status status,
                                              const FetchFailure& failure);

[[nodiscard]] std::optional<FetchFailure> GetFetchFailure(
    const absl::Status& status);

}

#endif
