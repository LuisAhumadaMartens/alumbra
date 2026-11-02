#include "umbra/manifest/manifest_error.h"

#include <cstdint>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "umbra/base/error_catalogue.h"

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
  return absl::InvalidArgumentError(absl::StrCat(Describe(), ": ", what));
}

}
