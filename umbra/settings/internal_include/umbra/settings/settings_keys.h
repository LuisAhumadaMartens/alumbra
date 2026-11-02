#ifndef UMBRA_SETTINGS_SETTINGS_KEYS_H_
#define UMBRA_SETTINGS_SETTINGS_KEYS_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "absl/time/time.h"
#include "umbra/settings/setting_types.h"

namespace umbra::settings {

enum class DecodePreferHardware {
  kAuto,
  kAlways,
  kNever,
};

enum class DemuxFfmpegMode {
  kFallback,
  kPrefer,
  kOff,
};

enum class ObservabilityLogLevel {
  kVerbose,
  kDebug,
  kInfo,
  kWarning,
  kError,
};

enum class PlaybackDisplayRateMatch {
  kOff,
  kSeamless,
  kAlways,
};

enum class PlaybackSyncMode {
  kAudio,
  kDisplayResample,
};

inline constexpr Key<absl::Duration> kAbrRenditionExclusionMs{0};
inline constexpr Key<std::vector<std::string>> kDecodeBlocklistExtraDecoders{1};
inline constexpr Key<std::vector<std::string>> kDecodeBlocklistIgnoredEntries{
    2};
inline constexpr Key<std::int64_t> kDecodeDroppedFramesMaxPercent{3};
inline constexpr Key<absl::Duration> kDecodeDroppedFramesWindowMs{4};
inline constexpr Key<bool> kDecodeFallbackEnabled{5};
inline constexpr Key<DecodePreferHardware> kDecodePreferHardware{6};
inline constexpr Key<DemuxFfmpegMode> kDemuxFfmpegMode{7};
inline constexpr Key<bool> kDemuxMp4EditListsEnabled{8};
inline constexpr Key<std::int64_t> kEngineThreadsDecoderMax{9};
inline constexpr Key<std::int64_t> kEngineThreadsMaxWorkers{10};
inline constexpr Key<absl::Duration> kEngineThreadsYieldBudgetMs{11};
inline constexpr Key<absl::Duration> kNetworkPathwayExclusionMs{12};
inline constexpr Key<absl::Duration> kNetworkRetryInitialBackoffMs{13};
inline constexpr Key<std::int64_t> kNetworkRetryMaxAttempts{14};
inline constexpr Key<absl::Duration> kNetworkRetryMaxBackoffMs{15};
inline constexpr Key<std::vector<std::string>> kObservabilityLogCategories{16};
inline constexpr Key<ObservabilityLogLevel> kObservabilityLogLevel{17};
inline constexpr Key<std::vector<std::string>> kObservabilityTraceCategories{
    18};
inline constexpr Key<PlaybackDisplayRateMatch> kPlaybackDisplayRateMatch{19};
inline constexpr Key<std::int64_t> kPlaybackSyncAudioOffsetMs{20};
inline constexpr Key<std::int64_t> kPlaybackSyncFrameDropLatenessPercent{21};
inline constexpr Key<absl::Duration> kPlaybackSyncKeyframeSkipLagMs{22};
inline constexpr Key<PlaybackSyncMode> kPlaybackSyncMode{23};
inline constexpr Key<double> kPlaybackSyncResampleMaxChangePercent{24};
inline constexpr Key<absl::Duration> kPlayerEventsTimeupdateIntervalMs{25};
inline constexpr Key<std::string> kSourceTypeHint{26};
inline constexpr Key<absl::Duration> kUiControlsAutoHideMs{27};
inline constexpr Key<std::vector<std::string>> kUiControlsHidden{28};
inline constexpr Key<std::vector<std::string>> kUiControlsOrder{29};
inline constexpr Key<absl::Duration> kUiControlsSeekStepMs{30};
inline constexpr Key<double> kUiControlsVolumeStep{31};
inline constexpr Key<absl::Duration> kUiSpinnerDelayMs{32};

[[nodiscard]] Registry GetRegistry();

}

namespace umbra::settings {

template <>
struct EnumNames<DecodePreferHardware> {
  static constexpr std::string_view kNames =
      "auto\n"
      "always\n"
      "never\n";
};

template <>
struct EnumNames<DemuxFfmpegMode> {
  static constexpr std::string_view kNames =
      "fallback\n"
      "prefer\n"
      "off\n";
};

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
struct EnumNames<PlaybackDisplayRateMatch> {
  static constexpr std::string_view kNames =
      "off\n"
      "seamless\n"
      "always\n";
};

template <>
struct EnumNames<PlaybackSyncMode> {
  static constexpr std::string_view kNames =
      "audio\n"
      "display_resample\n";
};

}

#endif
