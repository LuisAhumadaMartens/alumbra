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

enum class PlaybackSeekDefaultMode {
  kAccurate,
  kFast,
};

enum class PlaybackSeekFastModeSnap {
  kPrevious,
  kNearest,
};

enum class PlaybackSyncMode {
  kAudio,
  kDisplayResample,
};

enum class RenderVideoScalingFilter {
  kLinear,
  kNearest,
};

enum class TextAutoSelect {
  kNone,
  kForced,
  kPreferredLanguage,
};

inline constexpr Key<std::uint64_t> kAbrBandwidthMinSampleBytes{0};
inline constexpr Key<std::int64_t> kAbrDefaultBandwidthBps{1};
inline constexpr Key<bool> kAbrEnabled{2};
inline constexpr Key<absl::Duration> kAbrEstimatorFastHalfLifeMs{3};
inline constexpr Key<absl::Duration> kAbrEstimatorSlowHalfLifeMs{4};
inline constexpr Key<std::int64_t> kAbrMaxBitrateBps{5};
inline constexpr Key<std::int64_t> kAbrMaxHeight{6};
inline constexpr Key<absl::Duration> kAbrRenditionExclusionMs{7};
inline constexpr Key<absl::Duration> kAbrSwitchBufferMs{8};
inline constexpr Key<AudioDownmixMode> kAudioDownmixMode{9};
inline constexpr Key<std::int64_t> kAudioOutputChannels{10};
inline constexpr Key<std::string> kAudioOutputDevice{11};
inline constexpr Key<AudioResamplerQuality> kAudioResamplerQuality{12};
inline constexpr Key<std::uint64_t> kBufferAudioMaxBytes{13};
inline constexpr Key<absl::Duration> kBufferBackGoalMs{14};
inline constexpr Key<absl::Duration> kBufferForwardGoalMs{15};
inline constexpr Key<absl::Duration> kBufferRebufferThresholdMs{16};
inline constexpr Key<absl::Duration> kBufferStartThresholdMs{17};
inline constexpr Key<std::uint64_t> kBufferVideoMaxBytes{18};
inline constexpr Key<std::uint64_t> kDashMpdMaxBytes{19};
inline constexpr Key<std::vector<std::string>> kDecodeBlocklistExtraDecoders{
    20};
inline constexpr Key<std::vector<std::string>> kDecodeBlocklistIgnoredEntries{
    21};
inline constexpr Key<std::int64_t> kDecodeDroppedFramesMaxPercent{22};
inline constexpr Key<absl::Duration> kDecodeDroppedFramesWindowMs{23};
inline constexpr Key<std::vector<std::string>> kDecodeExcludedCodecs{24};
inline constexpr Key<bool> kDecodeFallbackEnabled{25};
inline constexpr Key<DecodePreferHardware> kDecodePreferHardware{26};
inline constexpr Key<std::int64_t> kDecodeVideoMaxHeight{27};
inline constexpr Key<std::vector<std::string>> kDecodeVideoPreferredDecoders{
    28};
inline constexpr Key<DemuxFfmpegMode> kDemuxFfmpegMode{29};
inline constexpr Key<bool> kDemuxMp4EditListsEnabled{30};
inline constexpr Key<std::int64_t> kDemuxTsProgramNumber{31};
inline constexpr Key<std::int64_t> kEngineThreadsDecoderMax{32};
inline constexpr Key<std::int64_t> kEngineThreadsMaxWorkers{33};
inline constexpr Key<absl::Duration> kEngineThreadsYieldBudgetMs{34};
inline constexpr Key<bool> kHlsParserStrict{35};
inline constexpr Key<bool> kNetworkCmcdEnabled{36};
inline constexpr Key<NetworkCmcdMode> kNetworkCmcdMode{37};
inline constexpr Key<bool> kNetworkHttpAllowHttpsToHttpRedirect{38};
inline constexpr Key<absl::Duration> kNetworkHttpConnectTimeoutMs{39};
inline constexpr Key<std::int64_t> kNetworkHttpMaxRedirects{40};
inline constexpr Key<absl::Duration> kNetworkHttpReadTimeoutMs{41};
inline constexpr Key<std::string> kNetworkHttpUserAgent{42};
inline constexpr Key<absl::Duration> kNetworkPathwayExclusionMs{43};
inline constexpr Key<absl::Duration> kNetworkRetryInitialBackoffMs{44};
inline constexpr Key<std::int64_t> kNetworkRetryMaxAttempts{45};
inline constexpr Key<absl::Duration> kNetworkRetryMaxBackoffMs{46};
inline constexpr Key<std::vector<std::string>> kObservabilityLogCategories{47};
inline constexpr Key<ObservabilityLogLevel> kObservabilityLogLevel{48};
inline constexpr Key<std::vector<std::string>> kObservabilityTraceCategories{
    49};
inline constexpr Key<PlaybackDisplayRateMatch> kPlaybackDisplayRateMatch{50};
inline constexpr Key<PlaybackSeekDefaultMode> kPlaybackSeekDefaultMode{51};
inline constexpr Key<PlaybackSeekFastModeSnap> kPlaybackSeekFastModeSnap{52};
inline constexpr Key<std::int64_t> kPlaybackSyncAudioOffsetMs{53};
inline constexpr Key<std::int64_t> kPlaybackSyncFrameDropLatenessPercent{54};
inline constexpr Key<absl::Duration> kPlaybackSyncKeyframeSkipLagMs{55};
inline constexpr Key<PlaybackSyncMode> kPlaybackSyncMode{56};
inline constexpr Key<double> kPlaybackSyncResampleMaxChangePercent{57};
inline constexpr Key<absl::Duration> kPlayerEventsTimeupdateIntervalMs{58};
inline constexpr Key<RenderVideoScalingFilter> kRenderVideoScalingFilter{59};
inline constexpr Key<std::uint64_t> kSourceProbeMaxBytes{60};
inline constexpr Key<std::uint64_t> kSourceReadAheadBytes{61};
inline constexpr Key<std::string> kSourceTypeHint{62};
inline constexpr Key<TextAutoSelect> kTextAutoSelect{63};
inline constexpr Key<bool> kTextCharsetAutoDetect{64};
inline constexpr Key<std::int64_t> kTextCueMemoryBudgetKb{65};
inline constexpr Key<std::string> kTextSrtFallbackEncoding{66};
inline constexpr Key<bool> kTextWebvttEnableStyleBlocks{67};
inline constexpr Key<std::int64_t> kTextWebvttMaxCueTextBytes{68};
inline constexpr Key<bool> kTracksAudioPreferDescriptions{69};
inline constexpr Key<std::vector<std::string>> kTracksAudioPreferredLanguages{
    70};
inline constexpr Key<absl::Duration> kUiControlsAutoHideMs{71};
inline constexpr Key<std::vector<std::string>> kUiControlsHidden{72};
inline constexpr Key<std::vector<std::string>> kUiControlsOrder{73};
inline constexpr Key<absl::Duration> kUiControlsSeekStepMs{74};
inline constexpr Key<double> kUiControlsVolumeStep{75};
inline constexpr Key<bool> kUiDoubleTapSeekEnabled{76};
inline constexpr Key<absl::Duration> kUiSpinnerDelayMs{77};

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
struct EnumNames<PlaybackSeekDefaultMode> {
  static constexpr std::string_view kNames =
      "accurate\n"
      "fast\n";
};

template <>
struct EnumNames<PlaybackSeekFastModeSnap> {
  static constexpr std::string_view kNames =
      "previous\n"
      "nearest\n";
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

template <>
struct EnumNames<TextAutoSelect> {
  static constexpr std::string_view kNames =
      "none\n"
      "forced\n"
      "preferred_language\n";
};

}

#endif
