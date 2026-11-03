#ifndef UMBRA_SETTINGS_SETTINGS_KEYS_H_
#define UMBRA_SETTINGS_SETTINGS_KEYS_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "absl/time/time.h"
#include "umbra/settings/setting_types.h"

namespace umbra::settings {

enum class AudioDownmixMode {
  kAuto,
  kStereo,
  kNone,
};

enum class AudioResamplerQuality {
  kLow,
  kMedium,
  kHigh,
};

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

enum class NetworkCmcdMode {
  kHeader,
  kQuery,
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

enum class RenderVideoScalingFilter {
  kLinear,
  kNearest,
};

inline constexpr Key<std::uint64_t> kAbrBandwidthMinSampleBytes{0};
inline constexpr Key<std::int64_t> kAbrDefaultBandwidthBps{1};
inline constexpr Key<absl::Duration> kAbrEstimatorFastHalfLifeMs{2};
inline constexpr Key<absl::Duration> kAbrEstimatorSlowHalfLifeMs{3};
inline constexpr Key<std::int64_t> kAbrMaxBitrateBps{4};
inline constexpr Key<std::int64_t> kAbrMaxHeight{5};
inline constexpr Key<absl::Duration> kAbrRenditionExclusionMs{6};
inline constexpr Key<absl::Duration> kAbrSwitchBufferMs{7};
inline constexpr Key<AudioDownmixMode> kAudioDownmixMode{8};
inline constexpr Key<std::int64_t> kAudioOutputChannels{9};
inline constexpr Key<AudioResamplerQuality> kAudioResamplerQuality{10};
inline constexpr Key<std::uint64_t> kBufferAudioMaxBytes{11};
inline constexpr Key<absl::Duration> kBufferBackGoalMs{12};
inline constexpr Key<absl::Duration> kBufferForwardGoalMs{13};
inline constexpr Key<absl::Duration> kBufferRebufferThresholdMs{14};
inline constexpr Key<absl::Duration> kBufferStartThresholdMs{15};
inline constexpr Key<std::uint64_t> kBufferVideoMaxBytes{16};
inline constexpr Key<std::uint64_t> kDashMpdMaxBytes{17};
inline constexpr Key<std::vector<std::string>> kDecodeBlocklistExtraDecoders{
    18};
inline constexpr Key<std::vector<std::string>> kDecodeBlocklistIgnoredEntries{
    19};
inline constexpr Key<std::int64_t> kDecodeDroppedFramesMaxPercent{20};
inline constexpr Key<absl::Duration> kDecodeDroppedFramesWindowMs{21};
inline constexpr Key<std::vector<std::string>> kDecodeExcludedCodecs{22};
inline constexpr Key<bool> kDecodeFallbackEnabled{23};
inline constexpr Key<DecodePreferHardware> kDecodePreferHardware{24};
inline constexpr Key<std::int64_t> kDecodeVideoMaxHeight{25};
inline constexpr Key<std::vector<std::string>> kDecodeVideoPreferredDecoders{
    26};
inline constexpr Key<DemuxFfmpegMode> kDemuxFfmpegMode{27};
inline constexpr Key<bool> kDemuxMp4EditListsEnabled{28};
inline constexpr Key<std::int64_t> kEngineThreadsDecoderMax{29};
inline constexpr Key<std::int64_t> kEngineThreadsMaxWorkers{30};
inline constexpr Key<absl::Duration> kEngineThreadsYieldBudgetMs{31};
inline constexpr Key<bool> kHlsParserStrict{32};
inline constexpr Key<bool> kNetworkCmcdEnabled{33};
inline constexpr Key<NetworkCmcdMode> kNetworkCmcdMode{34};
inline constexpr Key<absl::Duration> kNetworkPathwayExclusionMs{35};
inline constexpr Key<absl::Duration> kNetworkRetryInitialBackoffMs{36};
inline constexpr Key<std::int64_t> kNetworkRetryMaxAttempts{37};
inline constexpr Key<absl::Duration> kNetworkRetryMaxBackoffMs{38};
inline constexpr Key<std::vector<std::string>> kObservabilityLogCategories{39};
inline constexpr Key<ObservabilityLogLevel> kObservabilityLogLevel{40};
inline constexpr Key<std::vector<std::string>> kObservabilityTraceCategories{
    41};
inline constexpr Key<PlaybackDisplayRateMatch> kPlaybackDisplayRateMatch{42};
inline constexpr Key<std::int64_t> kPlaybackSyncAudioOffsetMs{43};
inline constexpr Key<std::int64_t> kPlaybackSyncFrameDropLatenessPercent{44};
inline constexpr Key<absl::Duration> kPlaybackSyncKeyframeSkipLagMs{45};
inline constexpr Key<PlaybackSyncMode> kPlaybackSyncMode{46};
inline constexpr Key<double> kPlaybackSyncResampleMaxChangePercent{47};
inline constexpr Key<absl::Duration> kPlayerEventsTimeupdateIntervalMs{48};
inline constexpr Key<RenderVideoScalingFilter> kRenderVideoScalingFilter{49};
inline constexpr Key<std::uint64_t> kSourceReadAheadBytes{50};
inline constexpr Key<std::string> kSourceTypeHint{51};
inline constexpr Key<bool> kTracksAudioPreferDescriptions{52};
inline constexpr Key<std::vector<std::string>> kTracksAudioPreferredLanguages{
    53};
inline constexpr Key<absl::Duration> kUiControlsAutoHideMs{54};
inline constexpr Key<std::vector<std::string>> kUiControlsHidden{55};
inline constexpr Key<std::vector<std::string>> kUiControlsOrder{56};
inline constexpr Key<absl::Duration> kUiControlsSeekStepMs{57};
inline constexpr Key<double> kUiControlsVolumeStep{58};
inline constexpr Key<bool> kUiDoubleTapSeekEnabled{59};
inline constexpr Key<absl::Duration> kUiSpinnerDelayMs{60};

[[nodiscard]] Registry GetRegistry();

}

namespace umbra::settings {

template <>
struct EnumNames<AudioDownmixMode> {
  static constexpr std::string_view kNames =
      "auto\n"
      "stereo\n"
      "none\n";
};

template <>
struct EnumNames<AudioResamplerQuality> {
  static constexpr std::string_view kNames =
      "low\n"
      "medium\n"
      "high\n";
};

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
struct EnumNames<NetworkCmcdMode> {
  static constexpr std::string_view kNames =
      "header\n"
      "query\n";
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

template <>
struct EnumNames<RenderVideoScalingFilter> {
  static constexpr std::string_view kNames =
      "linear\n"
      "nearest\n";
};

}

#endif
