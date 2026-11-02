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
  kNetworkSchemeUnsupported = 1004,
  kNetworkUrlInvalid = 1005,
  kManifestInvalid = 2001,
  kMediaCorrupt = 3001,
  kSourceTypeUnsupported = 3002,
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

inline constexpr std::array<ErrorCatalogueEntry, 17> kErrorCatalogue{{
    {.code = ErrorCode::kNetworkTimeout,
     .name = "NetworkTimeout",
     .category = ErrorCategory::kNetwork,
     .severity = ErrorSeverity::kRecoverable,
     .retryable = true,
     .media_error = 2,
     .canonical = "Unavailable",
     .description = "Network request timed out"},
    {.code = ErrorCode::kNetworkAccessDenied,
     .name = "NetworkAccessDenied",
     .category = ErrorCategory::kNetwork,
     .severity = ErrorSeverity::kFatal,
     .retryable = false,
     .media_error = 2,
     .canonical = "PermissionDenied",
     .description = "Network request denied"},
    {.code = ErrorCode::kNetworkRequestFailed,
     .name = "NetworkRequestFailed",
     .category = ErrorCategory::kNetwork,
     .severity = ErrorSeverity::kFatal,
     .retryable = false,
     .media_error = 2,
     .canonical = "Unavailable",
     .description = "Network request failed"},
    {.code = ErrorCode::kNetworkSchemeUnsupported,
     .name = "NetworkSchemeUnsupported",
     .category = ErrorCategory::kNetwork,
     .severity = ErrorSeverity::kFatal,
     .retryable = false,
     .media_error = 4,
     .canonical = "Unimplemented",
     .description = "URL scheme is not supported"},
    {.code = ErrorCode::kNetworkUrlInvalid,
     .name = "NetworkUrlInvalid",
     .category = ErrorCategory::kNetwork,
     .severity = ErrorSeverity::kFatal,
     .retryable = false,
     .media_error = 4,
     .canonical = "InvalidArgument",
     .description = "URL is invalid"},
    {.code = ErrorCode::kManifestInvalid,
     .name = "ManifestInvalid",
     .category = ErrorCategory::kManifest,
     .severity = ErrorSeverity::kFatal,
     .retryable = false,
     .media_error = 4,
     .canonical = "InvalidArgument",
     .description = "Manifest is invalid"},
    {.code = ErrorCode::kMediaCorrupt,
     .name = "MediaCorrupt",
     .category = ErrorCategory::kDemux,
     .severity = ErrorSeverity::kFatal,
     .retryable = false,
     .media_error = 3,
     .canonical = "DataLoss",
     .description = "Media data is corrupt"},
    {.code = ErrorCode::kSourceTypeUnsupported,
     .name = "SourceTypeUnsupported",
     .category = ErrorCategory::kDemux,
     .severity = ErrorSeverity::kFatal,
     .retryable = false,
     .media_error = 4,
     .canonical = "Unimplemented",
     .description = "Source type is not supported"},
    {.code = ErrorCode::kCodecUnsupported,
     .name = "CodecUnsupported",
     .category = ErrorCategory::kDecode,
     .severity = ErrorSeverity::kFatal,
     .retryable = false,
     .media_error = 4,
     .canonical = "Unimplemented",
     .description = "Codec is unsupported"},
    {.code = ErrorCode::kDecoderFallback,
     .name = "DecoderFallback",
     .category = ErrorCategory::kDecode,
     .severity = ErrorSeverity::kDegraded,
     .retryable = false,
     .media_error = 3,
     .canonical = "FailedPrecondition",
     .description = "Decoder requires a fallback"},
    {.code = ErrorCode::kDecoderInitializationFailed,
     .name = "DecoderInitializationFailed",
     .category = ErrorCategory::kDecode,
     .severity = ErrorSeverity::kFatal,
     .retryable = false,
     .media_error = 3,
     .canonical = "Internal",
     .description = "Video decoder failed to initialize"},
    {.code = ErrorCode::kDrmLicenseDenied,
     .name = "DrmLicenseDenied",
     .category = ErrorCategory::kDrm,
     .severity = ErrorSeverity::kFatal,
     .retryable = false,
     .media_error = 3,
     .canonical = "PermissionDenied",
     .description = "DRM licence denied"},
    {.code = ErrorCode::kTextInvalid,
     .name = "TextInvalid",
     .category = ErrorCategory::kText,
     .severity = ErrorSeverity::kDegraded,
     .retryable = false,
     .media_error = 3,
     .canonical = "InvalidArgument",
     .description = "Caption data is invalid"},
    {.code = ErrorCode::kAdsUnavailable,
     .name = "AdsUnavailable",
     .category = ErrorCategory::kAds,
     .severity = ErrorSeverity::kDegraded,
     .retryable = false,
     .media_error = 2,
     .canonical = "Unavailable",
     .description = "Ad source is unavailable"},
    {.code = ErrorCode::kSettingsInvalid,
     .name = "SettingsInvalid",
     .category = ErrorCategory::kPlayer,
     .severity = ErrorSeverity::kFatal,
     .retryable = false,
     .media_error = 4,
     .canonical = "InvalidArgument",
     .description = "Setting value is invalid"},
    {.code = ErrorCode::kPlayerCancelled,
     .name = "PlayerCancelled",
     .category = ErrorCategory::kPlayer,
     .severity = ErrorSeverity::kRecoverable,
     .retryable = false,
     .media_error = 1,
     .canonical = "Cancelled",
     .description = "Player operation cancelled"},
    {.code = ErrorCode::kPlayerInternal,
     .name = "PlayerInternal",
     .category = ErrorCategory::kPlayer,
     .severity = ErrorSeverity::kFatal,
     .retryable = false,
     .media_error = 3,
     .canonical = "Internal",
     .description = "Player invariant failed"},
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
