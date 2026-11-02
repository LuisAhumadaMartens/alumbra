#include "umbra/player/media_error.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "absl/strings/numbers.h"
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

template <typename Integer>
std::optional<Integer> ParseContext(const base::ErrorDetail& detail,
                                    const std::string& key) {
  const auto found = detail.context.find(key);
  Integer value{};
  if (found == detail.context.end() ||
      !absl::SimpleAtoi(found->second, &value)) {
    return std::nullopt;
  }
  return value;
}

}

MediaError::MediaError(MediaErrorCode code, base::ErrorDetail detail)
    : code_(code), message_(MessageFor(detail)), detail_(std::move(detail)) {}

std::optional<int64_t> MediaError::platform_code() const {
  return ParseContext<int64_t>(detail_, "platform_error_code");
}

std::optional<int> MediaError::http_status() const {
  return ParseContext<int>(detail_, "http_status");
}

}
