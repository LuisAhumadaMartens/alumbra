#ifndef UMBRA_SETTINGS_SETTINGS_KEYS_H_
#define UMBRA_SETTINGS_SETTINGS_KEYS_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "absl/time/time.h"
#include "umbra/settings/setting_types.h"

namespace umbra::settings {

enum class ObservabilityLogLevel {
  kVerbose,
  kDebug,
  kInfo,
  kWarning,
  kError,
};

enum class SourceTypeHint {
  kAuto,
  kHls,
  kDash,
  kMp4,
  kMatroska,
  kMpegts,
  kFlv,
};

inline constexpr Key<absl::Duration> kAbrRenditionExclusionMs{0};
inline constexpr Key<std::int64_t> kEngineThreadsDecoderMax{1};
inline constexpr Key<std::int64_t> kEngineThreadsMaxWorkers{2};
inline constexpr Key<absl::Duration> kEngineThreadsYieldBudgetMs{3};
inline constexpr Key<absl::Duration> kNetworkPathwayExclusionMs{4};
inline constexpr Key<absl::Duration> kNetworkRetryInitialBackoffMs{5};
inline constexpr Key<std::int64_t> kNetworkRetryMaxAttempts{6};
inline constexpr Key<absl::Duration> kNetworkRetryMaxBackoffMs{7};
inline constexpr Key<std::vector<std::string>> kObservabilityLogCategories{8};
inline constexpr Key<ObservabilityLogLevel> kObservabilityLogLevel{9};
inline constexpr Key<std::vector<std::string>> kObservabilityTraceCategories{
    10};
inline constexpr Key<absl::Duration> kPlayerEventsTimeupdateIntervalMs{11};
inline constexpr Key<std::uint64_t> kSourceReadAheadBytes{12};
inline constexpr Key<SourceTypeHint> kSourceTypeHint{13};

[[nodiscard]] Registry GetRegistry();

}

namespace umbra::settings {

template <>
struct EnumNames<ObservabilityLogLevel> {
  static constexpr std::string_view kNames =
      "verbose\n"
      "debug\n"
      "info\n"
      "warning\n"
      "error\n";
};

template <>
struct EnumNames<SourceTypeHint> {
  static constexpr std::string_view kNames =
      "auto\n"
      "hls\n"
      "dash\n"
      "mp4\n"
      "matroska\n"
      "mpegts\n"
      "flv\n";
};

}

#endif
