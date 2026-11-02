#include "umbra/base/error_catalogue.h"

static_assert(umbra::base::FindError(umbra::base::ErrorCode::kPlayerInternal)
                  ->severity == umbra::base::ErrorSeverity::kFatal);

int main() {
  const umbra::base::ErrorCatalogueEntry* entry =
      umbra::base::FindError(umbra::base::ErrorCode::kNetworkTimeout);
  const bool ok = entry != nullptr && entry->retryable &&
                  entry->category == umbra::base::ErrorCategory::kNetwork &&
                  entry->canonical == "Unavailable";
  return ok ? 0 : 1;
}
