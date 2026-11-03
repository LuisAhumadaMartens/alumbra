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
inline constexpr Key<absl::Duration> kAdsPrefetchLeadMs{10};
inline constexpr Key<AdsSeekPlaySkippedBreaks> kAdsSeekPlaySkippedBreaks{11};
inline constexpr Key<std::uint64_t> kAdsVastMaxResponseBytes{12};
inline constexpr Key<std::int64_t> kAdsVastMaxWrapperDepth{13};
inline constexpr Key<absl::Duration> kAdsVastRequestTimeoutMs{14};
inline constexpr Key<AudioDownmixMode> kAudioDownmixMode{15};
inline constexpr Key<std::int64_t> kAudioOutputChannels{16};
inline constexpr Key<std::string> kAudioOutputDevice{17};
inline constexpr Key<AudioResamplerQuality> kAudioResamplerQuality{18};
inline constexpr Key<std::uint64_t> kBufferAudioMaxBytes{19};
inline constexpr Key<absl::Duration> kBufferBackGoalMs{20};
inline constexpr Key<absl::Duration> kBufferForwardGoalMs{21};
inline constexpr Key<absl::Duration> kBufferRebufferThresholdMs{22};
inline constexpr Key<absl::Duration> kBufferStartThresholdMs{23};
inline constexpr Key<std::uint64_t> kBufferVideoMaxBytes{24};
inline constexpr Key<std::uint64_t> kDashMpdMaxBytes{25};
inline constexpr Key<std::vector<std::string>> kDecodeBlocklistExtraDecoders{
    26};
inline constexpr Key<std::vector<std::string>> kDecodeBlocklistIgnoredEntries{
    27};
inline constexpr Key<std::int64_t> kDecodeDroppedFramesMaxPercent{28};
inline constexpr Key<absl::Duration> kDecodeDroppedFramesWindowMs{29};
inline constexpr Key<std::vector<std::string>> kDecodeExcludedCodecs{30};
inline constexpr Key<bool> kDecodeFallbackEnabled{31};
inline constexpr Key<DecodePreferHardware> kDecodePreferHardware{32};
inline constexpr Key<std::int64_t> kDecodeVideoMaxHeight{33};
inline constexpr Key<std::vector<std::string>> kDecodeVideoPreferredDecoders{
    34};
inline constexpr Key<DemuxFfmpegMode> kDemuxFfmpegMode{35};
inline constexpr Key<bool> kDemuxMp4EditListsEnabled{36};
inline constexpr Key<std::int64_t> kDemuxTsProgramNumber{37};
inline constexpr Key<std::int64_t> kEngineThreadsDecoderMax{38};
inline constexpr Key<std::int64_t> kEngineThreadsMaxWorkers{39};
inline constexpr Key<absl::Duration> kEngineThreadsYieldBudgetMs{40};
inline constexpr Key<bool> kHlsParserStrict{41};
inline constexpr Key<bool> kNetworkCmcdEnabled{42};
inline constexpr Key<NetworkCmcdMode> kNetworkCmcdMode{43};
inline constexpr Key<bool> kNetworkHttpAllowHttpsToHttpRedirect{44};
inline constexpr Key<absl::Duration> kNetworkHttpConnectTimeoutMs{45};
inline constexpr Key<std::int64_t> kNetworkHttpMaxRedirects{46};
inline constexpr Key<absl::Duration> kNetworkHttpReadTimeoutMs{47};
inline constexpr Key<std::string> kNetworkHttpUserAgent{48};
inline constexpr Key<absl::Duration> kNetworkPathwayExclusionMs{49};
inline constexpr Key<absl::Duration> kNetworkRetryInitialBackoffMs{50};
inline constexpr Key<std::int64_t> kNetworkRetryMaxAttempts{51};
inline constexpr Key<absl::Duration> kNetworkRetryMaxBackoffMs{52};
inline constexpr Key<std::vector<std::string>> kObservabilityLogCategories{53};
inline constexpr Key<ObservabilityLogLevel> kObservabilityLogLevel{54};
inline constexpr Key<std::vector<std::string>> kObservabilityTraceCategories{
    55};
inline constexpr Key<PlaybackDisplayRateMatch> kPlaybackDisplayRateMatch{56};
inline constexpr Key<bool> kPlaybackPreservesPitch{57};
inline constexpr Key<double> kPlaybackRateMax{58};
inline constexpr Key<double> kPlaybackRateMin{59};
inline constexpr Key<PlaybackSeekDefaultMode> kPlaybackSeekDefaultMode{60};
inline constexpr Key<PlaybackSeekFastModeSnap> kPlaybackSeekFastModeSnap{61};
inline constexpr Key<std::int64_t> kPlaybackSyncAudioOffsetMs{62};
inline constexpr Key<std::int64_t> kPlaybackSyncFrameDropLatenessPercent{63};
inline constexpr Key<absl::Duration> kPlaybackSyncKeyframeSkipLagMs{64};
inline constexpr Key<PlaybackSyncMode> kPlaybackSyncMode{65};
inline constexpr Key<double> kPlaybackSyncResampleMaxChangePercent{66};
inline constexpr Key<absl::Duration> kPlayerEventsTimeupdateIntervalMs{67};
inline constexpr Key<bool> kQoeEnabled{68};
inline constexpr Key<std::int64_t> kQoeSnapshotIntervalMs{69};
inline constexpr Key<RenderVideoScalingFilter> kRenderVideoScalingFilter{70};
inline constexpr Key<std::uint64_t> kSourceProbeMaxBytes{71};
inline constexpr Key<std::uint64_t> kSourceReadAheadBytes{72};
inline constexpr Key<std::string> kSourceTypeHint{73};
inline constexpr Key<TextAutoSelect> kTextAutoSelect{74};
inline constexpr Key<bool> kTextCharsetAutoDetect{75};
inline constexpr Key<std::int64_t> kTextCueMemoryBudgetKb{76};
inline constexpr Key<std::string> kTextSrtFallbackEncoding{77};
inline constexpr Key<bool> kTextWebvttEnableStyleBlocks{78};
inline constexpr Key<std::int64_t> kTextWebvttMaxCueTextBytes{79};
inline constexpr Key<bool> kTracksAudioPreferDescriptions{80};
inline constexpr Key<std::vector<std::string>> kTracksAudioPreferredLanguages{
    81};
inline constexpr Key<absl::Duration> kUiControlsAutoHideMs{82};
inline constexpr Key<std::vector<std::string>> kUiControlsHidden{83};
inline constexpr Key<std::vector<std::string>> kUiControlsOrder{84};
inline constexpr Key<absl::Duration> kUiControlsSeekStepMs{85};
inline constexpr Key<double> kUiControlsVolumeStep{86};
inline constexpr Key<bool> kUiDoubleTapSeekEnabled{87};
inline constexpr Key<absl::Duration> kUiSpinnerDelayMs{88};

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
