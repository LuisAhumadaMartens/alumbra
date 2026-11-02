#ifndef UMBRA_PLAYER_MEDIA_ERROR_H_
#define UMBRA_PLAYER_MEDIA_ERROR_H_

#include <cstdint>
#include <optional>
#include <string>

#include "umbra/base/error_catalogue.h"
#include "umbra/base/error_detail.h"

namespace umbra::player {

enum class MediaErrorCode : int {
  kAborted = 1,
  kNetwork = 2,
  kDecode = 3,
  kSrcNotSupported = 4,
};

[[nodiscard]] constexpr MediaErrorCode MediaErrorCodeFor(
    base::ErrorCategory category) {
  switch (category) {
    case base::ErrorCategory::kNetwork:
    case base::ErrorCategory::kAds:
      return MediaErrorCode::kNetwork;
    case base::ErrorCategory::kManifest:
      return MediaErrorCode::kSrcNotSupported;
    case base::ErrorCategory::kDemux:
    case base::ErrorCategory::kDecode:
    case base::ErrorCategory::kDrm:
    case base::ErrorCategory::kText:
    case base::ErrorCategory::kPlayer:
      return MediaErrorCode::kDecode;
  }
  return MediaErrorCode::kDecode;
}

[[nodiscard]] constexpr MediaErrorCode MediaErrorCodeFor(
    const base::ErrorDetail& detail) {
  const base::ErrorCatalogueEntry* entry = base::FindError(detail.code);
  if (entry == nullptr || entry->media_error < 1 || entry->media_error > 4) {
    return MediaErrorCodeFor(detail.category);
  }
  return static_cast<MediaErrorCode>(entry->media_error);
}

class MediaError {
 public:
  MediaError(MediaErrorCode code, base::ErrorDetail detail);

  [[nodiscard]] MediaErrorCode code() const { return code_; }
  [[nodiscard]] const std::string& message() const { return message_; }
  [[nodiscard]] const base::ErrorDetail& detail() const { return detail_; }
  [[nodiscard]] base::ErrorCategory category() const {
    return detail_.category;
  }
  [[nodiscard]] std::optional<int64_t> platform_code() const;
  [[nodiscard]] std::optional<int> http_status() const;

 private:
  MediaErrorCode code_;
  std::string message_;
  base::ErrorDetail detail_;
};

}

#endif
