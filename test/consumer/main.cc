#include "penumbra/interface/interface.h"
#include "umbra/base/base.h"
#include "umbra/base/error_detail.h"
#include "umbra/base/url_redactor.h"

#ifndef PENUMBRA_INTERFACE_INTERFACE_H_
#error "The installed Penumbra interface header was not included."
#endif

static_assert(__cplusplus == 202002L);

int main() {
  const umbra::base::UrlRedactor redactor({1, 2});
  const auto url = redactor.RedactUrl("https://host/path-token?token=secret");
  const auto* entry =
      umbra::base::FindError(umbra::base::ErrorCode::kNetworkTimeout);
  const umbra::base::ErrorDetail detail;
  if (entry == nullptr || !entry->retryable ||
      detail.category != umbra::base::ErrorCategory::kPlayer ||
      !url.starts_with("https://host [url:") ||
      url.find("token") != std::string::npos ||
      url.find("secret") != std::string::npos) {
    return 1;
  }
  return umbra::base::detail::SkeletonLinkProbe();
}
