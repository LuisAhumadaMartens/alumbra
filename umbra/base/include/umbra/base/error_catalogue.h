#ifndef UMBRA_BASE_ERROR_CATALOGUE_H_
#define UMBRA_BASE_ERROR_CATALOGUE_H_

#include <array>
#include <cstdint>
#include <string_view>

namespace umbra::base {

enum class ErrorCategory {
  kNetwork,
  kManifest,
  kDemux,
  kDecode,
  kDrm,
  kText,
  kAds,
  kPlayer,
};

enum class ErrorSeverity {
  kRecoverable,
  kDegraded,
  kFatal,
};

enum class ErrorCode : std::uint32_t {
  kNetworkTimeout = 1001,
  kNetworkAccessDenied = 1002,
  kNetworkRequestFailed = 1003,
  kManifestInvalid = 2001,
  kMediaCorrupt = 3001,
  kCodecUnsupported = 4001,
  kDecoderFallback = 4002,
  kDecoderInitializationFailed = 4003,
  kDrmLicenseDenied = 5001,
  kTextInvalid = 6001,
  kAdsUnavailable = 7001,
  kSettingsInvalid = 8001,
  kPlayerCancelled = 8002,
  kPlayerInternal = 8003,
};

struct ErrorCatalogueEntry {
  ErrorCode code;
  std::string_view name;
  ErrorCategory category;
  ErrorSeverity severity;
  bool retryable;
  int media_error;
  std::string_view canonical;
  std::string_view description;
};

inline constexpr std::array<ErrorCatalogueEntry, 14> kErrorCatalogue{{
    {ErrorCode::kNetworkTimeout, "NetworkTimeout", ErrorCategory::kNetwork,
     ErrorSeverity::kRecoverable, true, 2, "Unavailable",
     "Network request timed out"},
    {ErrorCode::kNetworkAccessDenied, "NetworkAccessDenied",
     ErrorCategory::kNetwork, ErrorSeverity::kFatal, false, 2,
     "PermissionDenied", "Network request denied"},
    {ErrorCode::kNetworkRequestFailed, "NetworkRequestFailed",
     ErrorCategory::kNetwork, ErrorSeverity::kFatal, false, 2, "Unavailable",
     "Network request failed"},
    {ErrorCode::kManifestInvalid, "ManifestInvalid", ErrorCategory::kManifest,
     ErrorSeverity::kFatal, false, 4, "InvalidArgument", "Manifest is invalid"},
    {ErrorCode::kMediaCorrupt, "MediaCorrupt", ErrorCategory::kDemux,
     ErrorSeverity::kFatal, false, 3, "DataLoss", "Media data is corrupt"},
    {ErrorCode::kCodecUnsupported, "CodecUnsupported", ErrorCategory::kDecode,
     ErrorSeverity::kFatal, false, 4, "Unimplemented", "Codec is unsupported"},
    {ErrorCode::kDecoderFallback, "DecoderFallback", ErrorCategory::kDecode,
     ErrorSeverity::kDegraded, false, 3, "FailedPrecondition",
     "Decoder requires a fallback"},
    {ErrorCode::kDecoderInitializationFailed, "DecoderInitializationFailed",
     ErrorCategory::kDecode, ErrorSeverity::kFatal, false, 3, "Internal",
     "Video decoder failed to initialize"},
    {ErrorCode::kDrmLicenseDenied, "DrmLicenseDenied", ErrorCategory::kDrm,
     ErrorSeverity::kFatal, false, 3, "PermissionDenied", "DRM licence denied"},
    {ErrorCode::kTextInvalid, "TextInvalid", ErrorCategory::kText,
     ErrorSeverity::kDegraded, false, 3, "InvalidArgument",
     "Caption data is invalid"},
    {ErrorCode::kAdsUnavailable, "AdsUnavailable", ErrorCategory::kAds,
     ErrorSeverity::kDegraded, false, 2, "Unavailable",
     "Ad source is unavailable"},
    {ErrorCode::kSettingsInvalid, "SettingsInvalid", ErrorCategory::kPlayer,
     ErrorSeverity::kFatal, false, 4, "InvalidArgument",
     "Setting value is invalid"},
    {ErrorCode::kPlayerCancelled, "PlayerCancelled", ErrorCategory::kPlayer,
     ErrorSeverity::kRecoverable, false, 1, "Cancelled",
     "Player operation cancelled"},
    {ErrorCode::kPlayerInternal, "PlayerInternal", ErrorCategory::kPlayer,
     ErrorSeverity::kFatal, false, 3, "Internal", "Player invariant failed"},
}};

[[nodiscard]] constexpr const ErrorCatalogueEntry* FindError(ErrorCode code) {
  for (const auto& entry : kErrorCatalogue) {
    if (entry.code == code) {
      return &entry;
    }
  }
  return nullptr;
}

}

#endif
