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
inline constexpr Key<std::uint64_t> kAdsVastMaxResponseBytes{11};
inline constexpr Key<std::int64_t> kAdsVastMaxWrapperDepth{12};
inline constexpr Key<absl::Duration> kAdsVastRequestTimeoutMs{13};
inline constexpr Key<AudioDownmixMode> kAudioDownmixMode{14};
inline constexpr Key<std::int64_t> kAudioOutputChannels{15};
inline constexpr Key<std::string> kAudioOutputDevice{16};
inline constexpr Key<AudioResamplerQuality> kAudioResamplerQuality{17};
inline constexpr Key<std::uint64_t> kBufferAudioMaxBytes{18};
inline constexpr Key<absl::Duration> kBufferBackGoalMs{19};
inline constexpr Key<absl::Duration> kBufferForwardGoalMs{20};
inline constexpr Key<absl::Duration> kBufferRebufferThresholdMs{21};
inline constexpr Key<absl::Duration> kBufferStartThresholdMs{22};
inline constexpr Key<std::uint64_t> kBufferVideoMaxBytes{23};
inline constexpr Key<std::uint64_t> kDashMpdMaxBytes{24};
inline constexpr Key<std::vector<std::string>> kDecodeBlocklistExtraDecoders{
    25};
inline constexpr Key<std::vector<std::string>> kDecodeBlocklistIgnoredEntries{
    26};
inline constexpr Key<std::int64_t> kDecodeDroppedFramesMaxPercent{27};
inline constexpr Key<absl::Duration> kDecodeDroppedFramesWindowMs{28};
inline constexpr Key<std::vector<std::string>> kDecodeExcludedCodecs{29};
inline constexpr Key<bool> kDecodeFallbackEnabled{30};
inline constexpr Key<DecodePreferHardware> kDecodePreferHardware{31};
inline constexpr Key<std::int64_t> kDecodeVideoMaxHeight{32};
inline constexpr Key<std::vector<std::string>> kDecodeVideoPreferredDecoders{
    33};
inline constexpr Key<DemuxFfmpegMode> kDemuxFfmpegMode{34};
inline constexpr Key<bool> kDemuxMp4EditListsEnabled{35};
inline constexpr Key<std::int64_t> kDemuxTsProgramNumber{36};
inline constexpr Key<std::int64_t> kEngineThreadsDecoderMax{37};
inline constexpr Key<std::int64_t> kEngineThreadsMaxWorkers{38};
inline constexpr Key<absl::Duration> kEngineThreadsYieldBudgetMs{39};
inline constexpr Key<bool> kHlsParserStrict{40};
inline constexpr Key<bool> kNetworkCmcdEnabled{41};
inline constexpr Key<NetworkCmcdMode> kNetworkCmcdMode{42};
inline constexpr Key<bool> kNetworkHttpAllowHttpsToHttpRedirect{43};
inline constexpr Key<absl::Duration> kNetworkHttpConnectTimeoutMs{44};
inline constexpr Key<std::int64_t> kNetworkHttpMaxRedirects{45};
inline constexpr Key<absl::Duration> kNetworkHttpReadTimeoutMs{46};
inline constexpr Key<std::string> kNetworkHttpUserAgent{47};
inline constexpr Key<absl::Duration> kNetworkPathwayExclusionMs{48};
inline constexpr Key<absl::Duration> kNetworkRetryInitialBackoffMs{49};
inline constexpr Key<std::int64_t> kNetworkRetryMaxAttempts{50};
inline constexpr Key<absl::Duration> kNetworkRetryMaxBackoffMs{51};
inline constexpr Key<std::vector<std::string>> kObservabilityLogCategories{52};
inline constexpr Key<ObservabilityLogLevel> kObservabilityLogLevel{53};
inline constexpr Key<std::vector<std::string>> kObservabilityTraceCategories{
    54};
inline constexpr Key<PlaybackDisplayRateMatch> kPlaybackDisplayRateMatch{55};
inline constexpr Key<bool> kPlaybackPreservesPitch{56};
inline constexpr Key<double> kPlaybackRateMax{57};
inline constexpr Key<double> kPlaybackRateMin{58};
inline constexpr Key<PlaybackSeekDefaultMode> kPlaybackSeekDefaultMode{59};
inline constexpr Key<PlaybackSeekFastModeSnap> kPlaybackSeekFastModeSnap{60};
inline constexpr Key<std::int64_t> kPlaybackSyncAudioOffsetMs{61};
inline constexpr Key<std::int64_t> kPlaybackSyncFrameDropLatenessPercent{62};
inline constexpr Key<absl::Duration> kPlaybackSyncKeyframeSkipLagMs{63};
inline constexpr Key<PlaybackSyncMode> kPlaybackSyncMode{64};
inline constexpr Key<double> kPlaybackSyncResampleMaxChangePercent{65};
inline constexpr Key<absl::Duration> kPlayerEventsTimeupdateIntervalMs{66};
inline constexpr Key<bool> kQoeEnabled{67};
inline constexpr Key<std::int64_t> kQoeSnapshotIntervalMs{68};
inline constexpr Key<RenderVideoScalingFilter> kRenderVideoScalingFilter{69};
inline constexpr Key<std::uint64_t> kSourceProbeMaxBytes{70};
inline constexpr Key<std::uint64_t> kSourceReadAheadBytes{71};
inline constexpr Key<std::string> kSourceTypeHint{72};
inline constexpr Key<TextAutoSelect> kTextAutoSelect{73};
inline constexpr Key<bool> kTextCharsetAutoDetect{74};
inline constexpr Key<std::int64_t> kTextCueMemoryBudgetKb{75};
inline constexpr Key<std::string> kTextSrtFallbackEncoding{76};
inline constexpr Key<bool> kTextWebvttEnableStyleBlocks{77};
inline constexpr Key<std::int64_t> kTextWebvttMaxCueTextBytes{78};
inline constexpr Key<bool> kTracksAudioPreferDescriptions{79};
inline constexpr Key<std::vector<std::string>> kTracksAudioPreferredLanguages{
    80};
inline constexpr Key<absl::Duration> kUiControlsAutoHideMs{81};
inline constexpr Key<std::vector<std::string>> kUiControlsHidden{82};
inline constexpr Key<std::vector<std::string>> kUiControlsOrder{83};
inline constexpr Key<absl::Duration> kUiControlsSeekStepMs{84};
inline constexpr Key<double> kUiControlsVolumeStep{85};
inline constexpr Key<bool> kUiDoubleTapSeekEnabled{86};
inline constexpr Key<absl::Duration> kUiSpinnerDelayMs{87};

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
