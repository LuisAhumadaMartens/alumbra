#include "umbra/manifest/manifest_error.h"

#include <cstdint>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/error_detail.h"
#include "umbra/base/status.h"
#include "umbra/base/url_redactor.h"

namespace umbra::manifest {
namespace {

std::string Describe() {
  for (const base::ErrorCatalogueEntry& entry : base::kErrorCatalogue) {
    if (entry.code == base::ErrorCode::kManifestInvalid) {
      return absl::StrCat("ALUMBRA-", static_cast<std::uint32_t>(entry.code),
                          ": ", entry.description);
    }
  }
  return "ALUMBRA: unknown error";
}

}

absl::Status InvalidManifest(std::string_view what) {
  const absl::Status status =
      absl::InvalidArgumentError(absl::StrCat(Describe(), ": ", what));
  const base::ErrorDetail detail{base::ErrorCode::kManifestInvalid,
                                 base::ErrorCategory::kManifest,
                                 base::ErrorSeverity::kFatal,
                                 false,
                                 {},
                                 {}};
  const base::UrlRedactor redactor;
  return base::AttachErrorDetail(status, detail, redactor);
}

}
