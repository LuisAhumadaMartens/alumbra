#include "umbra/base/status.h"

[[nodiscard]] absl::Status Fallible();
[[nodiscard]] absl::StatusOr<int> FallibleValue();

void UseStatus() {
#ifdef ALUMBRA_IGNORE_STATUS
  Fallible();
  FallibleValue();
  umbra::base::MakeError(umbra::base::ErrorCode::kPlayerInternal,
                         umbra::base::UrlRedactor{});
#else
  const auto status = Fallible();
  const auto value = FallibleValue();
  const auto generated = umbra::base::MakeError(
      umbra::base::ErrorCode::kPlayerInternal, umbra::base::UrlRedactor{});
  (void)status;
  (void)value;
  (void)generated;
#endif
}
