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

enum class TextEdge {
  kNone,
  kRaised,
  kDepressed,
  kUniform,
  kDropShadow,
};

enum class TextFontFamily {
  kDefault,
  kMonospaceSerif,
  kProportionalSerif,
  kMonospaceSans,
  kProportionalSans,
  kCasual,
  kCursive,
  kSmallCaps,
};

enum class TextStyleSource {
  kSettings,
  kPlatform,
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
inline constexpr Key<std::int64_t> kAdsBeaconTimeoutMs{10};
inline constexpr Key<bool> kAdsConsentChildDirectedEnabled{11};
inline constexpr Key<std::string> kAdsConsentGppString{12};
inline constexpr Key<bool> kAdsConsentLimitedAdsEnabled{13};
inline constexpr Key<std::string> kAdsConsentTcfString{14};
inline constexpr Key<bool> kAdsEnabled{15};
inline constexpr Key<bool> kAdsPreloadEnabled{16};
inline constexpr Key<std::int64_t> kAdsRequestTimeoutMs{17};
inline constexpr Key<AdsSeekPlaySkippedBreaks> kAdsSeekPlaySkippedBreaks{18};
inline constexpr Key<bool> kAdsUiShowCountdown{19};
inline constexpr Key<bool> kAdsUiShowPodPosition{20};
inline constexpr Key<AudioDownmixMode> kAudioDownmixMode{21};
inline constexpr Key<std::int64_t> kAudioOutputChannels{22};
inline constexpr Key<std::string> kAudioOutputDevice{23};
inline constexpr Key<AudioResamplerQuality> kAudioResamplerQuality{24};
inline constexpr Key<std::uint64_t> kBufferAudioMaxBytes{25};
inline constexpr Key<absl::Duration> kBufferBackGoalMs{26};
inline constexpr Key<absl::Duration> kBufferForwardGoalMs{27};
inline constexpr Key<absl::Duration> kBufferRebufferThresholdMs{28};
inline constexpr Key<absl::Duration> kBufferStartThresholdMs{29};
inline constexpr Key<std::uint64_t> kBufferVideoMaxBytes{30};
inline constexpr Key<std::uint64_t> kDashMpdMaxBytes{31};
inline constexpr Key<std::vector<std::string>> kDecodeBlocklistExtraDecoders{
    32};
inline constexpr Key<std::vector<std::string>> kDecodeBlocklistIgnoredEntries{
    33};
inline constexpr Key<std::int64_t> kDecodeDroppedFramesMaxPercent{34};
inline constexpr Key<absl::Duration> kDecodeDroppedFramesWindowMs{35};
inline constexpr Key<std::vector<std::string>> kDecodeExcludedCodecs{36};
inline constexpr Key<bool> kDecodeFallbackEnabled{37};
inline constexpr Key<DecodePreferHardware> kDecodePreferHardware{38};
inline constexpr Key<std::int64_t> kDecodeVideoMaxHeight{39};
inline constexpr Key<std::vector<std::string>> kDecodeVideoPreferredDecoders{
    40};
inline constexpr Key<DemuxFfmpegMode> kDemuxFfmpegMode{41};
inline constexpr Key<bool> kDemuxMp4EditListsEnabled{42};
inline constexpr Key<std::int64_t> kDemuxTsProgramNumber{43};
inline constexpr Key<std::int64_t> kEngineThreadsDecoderMax{44};
inline constexpr Key<std::int64_t> kEngineThreadsMaxWorkers{45};
inline constexpr Key<absl::Duration> kEngineThreadsYieldBudgetMs{46};
inline constexpr Key<bool> kHlsLiveStalePlaylistFailover{47};
inline constexpr Key<std::int64_t> kHlsLiveStartOffsetMs{48};
inline constexpr Key<bool> kHlsParserStrict{49};
inline constexpr Key<bool> kNetworkCmcdEnabled{50};
inline constexpr Key<NetworkCmcdMode> kNetworkCmcdMode{51};
inline constexpr Key<bool> kNetworkHttpAllowHttpsToHttpRedirect{52};
inline constexpr Key<absl::Duration> kNetworkHttpConnectTimeoutMs{53};
inline constexpr Key<std::int64_t> kNetworkHttpMaxRedirects{54};
inline constexpr Key<absl::Duration> kNetworkHttpReadTimeoutMs{55};
inline constexpr Key<std::string> kNetworkHttpUserAgent{56};
inline constexpr Key<absl::Duration> kNetworkPathwayExclusionMs{57};
inline constexpr Key<absl::Duration> kNetworkRetryInitialBackoffMs{58};
inline constexpr Key<std::int64_t> kNetworkRetryMaxAttempts{59};
inline constexpr Key<absl::Duration> kNetworkRetryMaxBackoffMs{60};
inline constexpr Key<std::vector<std::string>> kObservabilityLogCategories{61};
inline constexpr Key<ObservabilityLogLevel> kObservabilityLogLevel{62};
inline constexpr Key<std::vector<std::string>> kObservabilityTraceCategories{
    63};
inline constexpr Key<PlaybackDisplayRateMatch> kPlaybackDisplayRateMatch{64};
inline constexpr Key<bool> kPlaybackPreservesPitch{65};
inline constexpr Key<double> kPlaybackRateMax{66};
inline constexpr Key<double> kPlaybackRateMin{67};
inline constexpr Key<PlaybackSeekDefaultMode> kPlaybackSeekDefaultMode{68};
inline constexpr Key<PlaybackSeekFastModeSnap> kPlaybackSeekFastModeSnap{69};
inline constexpr Key<std::int64_t> kPlaybackSyncAudioOffsetMs{70};
inline constexpr Key<std::int64_t> kPlaybackSyncFrameDropLatenessPercent{71};
inline constexpr Key<absl::Duration> kPlaybackSyncKeyframeSkipLagMs{72};
inline constexpr Key<PlaybackSyncMode> kPlaybackSyncMode{73};
inline constexpr Key<double> kPlaybackSyncResampleMaxChangePercent{74};
inline constexpr Key<absl::Duration> kPlayerEventsTimeupdateIntervalMs{75};
inline constexpr Key<bool> kQoeEnabled{76};
inline constexpr Key<std::int64_t> kQoeSnapshotIntervalMs{77};
inline constexpr Key<RenderVideoScalingFilter> kRenderVideoScalingFilter{78};
inline constexpr Key<std::uint64_t> kSourceProbeMaxBytes{79};
inline constexpr Key<std::uint64_t> kSourceReadAheadBytes{80};
inline constexpr Key<std::string> kSourceTypeHint{81};
inline constexpr Key<bool> kTextAssEnabled{82};
inline constexpr Key<TextAutoSelect> kTextAutoSelect{83};
inline constexpr Key<std::string> kTextBackgroundColour{84};
inline constexpr Key<std::int64_t> kTextBackgroundOpacityPercent{85};
inline constexpr Key<bool> kTextCharsetAutoDetect{86};
inline constexpr Key<std::int64_t> kTextCueMemoryBudgetKb{87};
inline constexpr Key<TextEdge> kTextEdge{88};
inline constexpr Key<TextFontFamily> kTextFontFamily{89};
inline constexpr Key<std::int64_t> kTextFontSizePercent{90};
inline constexpr Key<std::int64_t> kTextPositionOffsetPercent{91};
inline constexpr Key<std::string> kTextSrtFallbackEncoding{92};
inline constexpr Key<TextStyleSource> kTextStyleSource{93};
inline constexpr Key<std::string> kTextTextColour{94};
inline constexpr Key<std::int64_t> kTextTextOpacityPercent{95};
inline constexpr Key<bool> kTextWebvttEnableStyleBlocks{96};
inline constexpr Key<std::int64_t> kTextWebvttMaxCueTextBytes{97};
inline constexpr Key<std::string> kTextWindowColour{98};
inline constexpr Key<std::int64_t> kTextWindowOpacityPercent{99};
inline constexpr Key<bool> kTracksAudioPreferDescriptions{100};
inline constexpr Key<std::vector<std::string>> kTracksAudioPreferredLanguages{
    101};
inline constexpr Key<absl::Duration> kUiControlsAutoHideMs{102};
inline constexpr Key<std::vector<std::string>> kUiControlsHidden{103};
inline constexpr Key<std::vector<std::string>> kUiControlsOrder{104};
inline constexpr Key<absl::Duration> kUiControlsSeekStepMs{105};
inline constexpr Key<double> kUiControlsVolumeStep{106};
inline constexpr Key<bool> kUiDoubleTapSeekEnabled{107};
inline constexpr Key<absl::Duration> kUiSpinnerDelayMs{108};

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

template <>
struct EnumNames<TextEdge> {
  static constexpr std::string_view kNames =
      "none\n"
      "raised\n"
      "depressed\n"
      "uniform\n"
      "drop_shadow\n";
};

template <>
struct EnumNames<TextFontFamily> {
  static constexpr std::string_view kNames =
      "default\n"
      "monospace_serif\n"
      "proportional_serif\n"
      "monospace_sans\n"
      "proportional_sans\n"
      "casual\n"
      "cursive\n"
      "small_caps\n";
};

template <>
struct EnumNames<TextStyleSource> {
  static constexpr std::string_view kNames =
      "settings\n"
      "platform\n";
};

}

#endif
