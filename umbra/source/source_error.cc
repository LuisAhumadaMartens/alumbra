#include "umbra/source/source_error.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/error_detail.h"
#include "umbra/base/status.h"
#include "umbra/base/url_redactor.h"

namespace umbra::source {

absl::Status MakeSourceError(base::ErrorCode code, absl::StatusCode status_code,
                             std::string_view message,
                             std::map<std::string, std::string> context,
                             const base::UrlRedactor& redactor) {
  const base::ErrorCatalogueEntry* entry = base::FindError(code);
  if (entry == nullptr) {
    return absl::InternalError("unknown error code");
  }
  const base::ErrorDetail detail(entry->code, entry->category, entry->severity,
                                 entry->retryable, std::move(context), {});
  return base::AttachErrorDetail(
      absl::Status(
          status_code,
          absl::StrCat("ALUMBRA-", static_cast<uint32_t>(code), ": ", message)),
      detail, redactor);
}

}
