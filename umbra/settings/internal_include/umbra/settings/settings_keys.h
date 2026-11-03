#ifndef UMBRA_SETTINGS_SETTINGS_KEYS_H_
#define UMBRA_SETTINGS_SETTINGS_KEYS_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "absl/time/time.h"
#include "umbra/settings/setting_types.h"

namespace umbra::settings {

enum class AdsSeekPlaySkippedBreaks {
  kLast,
  kAll,
  kNone,
};

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
inline constexpr Key<bool> kAbrCapToViewport{1};
inline constexpr Key<std::int64_t> kAbrDefaultBandwidthBps{2};
inline constexpr Key<bool> kAbrEnabled{3};
inline constexpr Key<absl::Duration> kAbrEstimatorFastHalfLifeMs{4};
inline constexpr Key<absl::Duration> kAbrEstimatorSlowHalfLifeMs{5};
inline constexpr Key<std::int64_t> kAbrMaxBitrateBps{6};
inline constexpr Key<std::int64_t> kAbrMaxHeight{7};
inline constexpr Key<absl::Duration> kAbrRenditionExclusionMs{8};
inline constexpr Key<absl::Duration> kAbrSwitchBufferMs{9};
inline constexpr Key<AdsSeekPlaySkippedBreaks> kAdsSeekPlaySkippedBreaks{10};
inline constexpr Key<AudioDownmixMode> kAudioDownmixMode{11};
inline constexpr Key<std::int64_t> kAudioOutputChannels{12};
inline constexpr Key<std::string> kAudioOutputDevice{13};
inline constexpr Key<AudioResamplerQuality> kAudioResamplerQuality{14};
inline constexpr Key<std::uint64_t> kBufferAudioMaxBytes{15};
inline constexpr Key<absl::Duration> kBufferBackGoalMs{16};
inline constexpr Key<absl::Duration> kBufferForwardGoalMs{17};
inline constexpr Key<absl::Duration> kBufferRebufferThresholdMs{18};
inline constexpr Key<absl::Duration> kBufferStartThresholdMs{19};
inline constexpr Key<std::uint64_t> kBufferVideoMaxBytes{20};
inline constexpr Key<std::uint64_t> kDashMpdMaxBytes{21};
inline constexpr Key<std::vector<std::string>> kDecodeBlocklistExtraDecoders{
    22};
inline constexpr Key<std::vector<std::string>> kDecodeBlocklistIgnoredEntries{
    23};
inline constexpr Key<std::int64_t> kDecodeDroppedFramesMaxPercent{24};
inline constexpr Key<absl::Duration> kDecodeDroppedFramesWindowMs{25};
inline constexpr Key<std::vector<std::string>> kDecodeExcludedCodecs{26};
inline constexpr Key<bool> kDecodeFallbackEnabled{27};
inline constexpr Key<DecodePreferHardware> kDecodePreferHardware{28};
inline constexpr Key<std::int64_t> kDecodeVideoMaxHeight{29};
inline constexpr Key<std::vector<std::string>> kDecodeVideoPreferredDecoders{
    30};
inline constexpr Key<DemuxFfmpegMode> kDemuxFfmpegMode{31};
inline constexpr Key<bool> kDemuxMp4EditListsEnabled{32};
inline constexpr Key<std::int64_t> kDemuxTsProgramNumber{33};
inline constexpr Key<std::int64_t> kEngineThreadsDecoderMax{34};
inline constexpr Key<std::int64_t> kEngineThreadsMaxWorkers{35};
inline constexpr Key<absl::Duration> kEngineThreadsYieldBudgetMs{36};
inline constexpr Key<bool> kHlsParserStrict{37};
inline constexpr Key<bool> kNetworkCmcdEnabled{38};
inline constexpr Key<NetworkCmcdMode> kNetworkCmcdMode{39};
inline constexpr Key<bool> kNetworkHttpAllowHttpsToHttpRedirect{40};
inline constexpr Key<absl::Duration> kNetworkHttpConnectTimeoutMs{41};
inline constexpr Key<std::int64_t> kNetworkHttpMaxRedirects{42};
inline constexpr Key<absl::Duration> kNetworkHttpReadTimeoutMs{43};
inline constexpr Key<std::string> kNetworkHttpUserAgent{44};
inline constexpr Key<absl::Duration> kNetworkPathwayExclusionMs{45};
inline constexpr Key<absl::Duration> kNetworkRetryInitialBackoffMs{46};
inline constexpr Key<std::int64_t> kNetworkRetryMaxAttempts{47};
inline constexpr Key<absl::Duration> kNetworkRetryMaxBackoffMs{48};
inline constexpr Key<std::vector<std::string>> kObservabilityLogCategories{49};
inline constexpr Key<ObservabilityLogLevel> kObservabilityLogLevel{50};
inline constexpr Key<std::vector<std::string>> kObservabilityTraceCategories{
    51};
inline constexpr Key<PlaybackDisplayRateMatch> kPlaybackDisplayRateMatch{52};
inline constexpr Key<bool> kPlaybackPreservesPitch{53};
inline constexpr Key<double> kPlaybackRateMax{54};
inline constexpr Key<double> kPlaybackRateMin{55};
inline constexpr Key<PlaybackSeekDefaultMode> kPlaybackSeekDefaultMode{56};
inline constexpr Key<PlaybackSeekFastModeSnap> kPlaybackSeekFastModeSnap{57};
inline constexpr Key<std::int64_t> kPlaybackSyncAudioOffsetMs{58};
inline constexpr Key<std::int64_t> kPlaybackSyncFrameDropLatenessPercent{59};
inline constexpr Key<absl::Duration> kPlaybackSyncKeyframeSkipLagMs{60};
inline constexpr Key<PlaybackSyncMode> kPlaybackSyncMode{61};
inline constexpr Key<double> kPlaybackSyncResampleMaxChangePercent{62};
inline constexpr Key<absl::Duration> kPlayerEventsTimeupdateIntervalMs{63};
inline constexpr Key<RenderVideoScalingFilter> kRenderVideoScalingFilter{64};
inline constexpr Key<std::uint64_t> kSourceProbeMaxBytes{65};
inline constexpr Key<std::uint64_t> kSourceReadAheadBytes{66};
inline constexpr Key<std::string> kSourceTypeHint{67};
inline constexpr Key<TextAutoSelect> kTextAutoSelect{68};
inline constexpr Key<bool> kTextCharsetAutoDetect{69};
inline constexpr Key<std::int64_t> kTextCueMemoryBudgetKb{70};
inline constexpr Key<std::string> kTextSrtFallbackEncoding{71};
inline constexpr Key<bool> kTextWebvttEnableStyleBlocks{72};
inline constexpr Key<std::int64_t> kTextWebvttMaxCueTextBytes{73};
inline constexpr Key<bool> kTracksAudioPreferDescriptions{74};
inline constexpr Key<std::vector<std::string>> kTracksAudioPreferredLanguages{
    75};
inline constexpr Key<absl::Duration> kUiControlsAutoHideMs{76};
inline constexpr Key<std::vector<std::string>> kUiControlsHidden{77};
inline constexpr Key<std::vector<std::string>> kUiControlsOrder{78};
inline constexpr Key<absl::Duration> kUiControlsSeekStepMs{79};
inline constexpr Key<double> kUiControlsVolumeStep{80};
inline constexpr Key<bool> kUiDoubleTapSeekEnabled{81};
inline constexpr Key<absl::Duration> kUiSpinnerDelayMs{82};

[[nodiscard]] Registry GetRegistry();

}

namespace umbra::settings {

template <>
struct EnumNames<AdsSeekPlaySkippedBreaks> {
  static constexpr std::string_view kNames =
      "last\n"
      "all\n"
      "none\n";
};

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
