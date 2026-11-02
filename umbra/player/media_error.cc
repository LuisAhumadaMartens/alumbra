#include "umbra/player/media_error.h"

#include <cstdint>
#include <string>
#include <utility>

#include "absl/strings/str_cat.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/error_detail.h"

namespace umbra::player {
namespace {

std::string MessageFor(const base::ErrorDetail& detail) {
  const base::ErrorCatalogueEntry* entry = base::FindError(detail.code);
  const auto number = static_cast<std::uint32_t>(detail.code);
  if (entry == nullptr) {
    return absl::StrCat("ALUMBRA-", absl::Dec(number, absl::kZeroPad4),
                        ": Unknown error");
  }
  return absl::StrCat("ALUMBRA-", absl::Dec(number, absl::kZeroPad4), ": ",
                      entry->description);
}

}

MediaError::MediaError(MediaErrorCode code, base::ErrorDetail detail)
    : code_(code), message_(MessageFor(detail)), detail_(std::move(detail)) {}

}
