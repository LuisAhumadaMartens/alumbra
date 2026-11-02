#include "penumbra/desktop/capability_provider.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"
#include "penumbra/desktop/codec_string.h"
#include "penumbra/desktop/decoder_probe.h"
#include "penumbra/desktop/decoder_table.h"
#include "penumbra/interface/encryption_info.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/stream_info.h"

extern "C" {
#include "dav1d/dav1d.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/codec.h"
}

#include "penumbra/desktop/software_decoder_probe.h"

namespace penumbra::desktop {
namespace {

class FakeProbe : public DecoderProbe {
 public:
  static std::shared_ptr<FakeProbe> WithEveryTableDecoder() {
    auto probe = std::make_shared<FakeProbe>();
    for (const DecoderEntry& entry : SoftwareDecoderTable()) {
      probe->available.emplace(entry.decoder_name);
    }
    return probe;
  }

  absl::StatusOr<bool> IsCompiledIn(
      std::string_view decoder_name) const override {
    ++calls;
    if (failure.has_value()) {
      return *failure;
    }
    return available.contains(std::string(decoder_name));
  }

  std::set<std::string> available;
  std::optional<absl::Status> failure;
  mutable int calls = 0;
};

CapabilityRequest Video(CodecId codec, std::string codec_string, int32_t width,
                        int32_t height, int64_t frames_per_second) {
  CapabilityRequest request;
  request.stream.codec = codec;
  request.stream.codec_string = std::move(codec_string);
  VideoInfo video;
  video.coded_width = width;
  video.coded_height = height;
  video.display_width = width;
  video.display_height = height;
  const std::optional<Rational> rate = Rational::Create(frames_per_second, 1);
  if (rate.has_value()) {
    video.frame_rate = *rate;
  }
  request.stream.format = video;
  request.mime_type = "video/mp4";
  return request;
}

CapabilityRequest Audio(CodecId codec, std::string codec_string) {
  CapabilityRequest request;
  request.stream.codec = codec;
  request.stream.codec_string = std::move(codec_string);
  request.stream.format = AudioInfo{.sample_rate = 48000, .channel_count = 2};
  request.mime_type = "audio/mp4";
  return request;
}

DesktopCapabilityProvider MakeProvider(
    DecodeLimits limits = {},
    std::shared_ptr<FakeProbe> probe = FakeProbe::WithEveryTableDecoder()) {
  return {std::move(limits), std::move(probe)};
}

DecodingCapability Query(const DesktopCapabilityProvider& provider,
                         const CapabilityRequest& request) {
  const auto result = provider.QueryDecoding(request);
  EXPECT_TRUE(result.ok()) << result.status();
  if (!result.ok()) {
    return {};
  }
  return *result;
}

TEST(CapabilityProviderTest, EveryM1CodecIsSupported) {
  const DesktopCapabilityProvider provider = MakeProvider();
  const auto video_codecs = std::to_array<std::pair<CodecId, std::string>>({
      {CodecId::kH264, "avc1.64001f"},
      {CodecId::kHevc, "hvc1.1.6.L93.B0"},
      {CodecId::kVp9, "vp09.00.10.08"},
      {CodecId::kAv1, "av01.0.08M.08"},
  });
  for (const auto& [codec, codec_string] : video_codecs) {
    EXPECT_TRUE(
        Query(provider, Video(codec, codec_string, 1280, 720, 30)).supported)
        << codec_string;
  }
  const auto audio_codecs = std::to_array<std::pair<CodecId, std::string>>({
      {CodecId::kAac, "mp4a.40.2"},
      {CodecId::kOpus, "opus"},
      {CodecId::kAc3, "ac-3"},
      {CodecId::kEac3, "ec-3"},
      {CodecId::kFlac, "flac"},
      {CodecId::kMp3, "mp4a.6B"},
  });
  for (const auto& [codec, codec_string] : audio_codecs) {
    EXPECT_TRUE(Query(provider, Audio(codec, codec_string)).supported)
        << codec_string;
  }
  EXPECT_EQ(video_codecs.size() + audio_codecs.size(),
            SoftwareDecoderTable().size());
}

TEST(CapabilityProviderTest, CodecIdAloneIsEnough) {
  const DesktopCapabilityProvider provider = MakeProvider();
  for (const DecoderEntry& entry : SoftwareDecoderTable()) {
    const CapabilityRequest request = entry.smooth_luma_samples_per_second > 0
                                          ? Video(entry.codec, "", 640, 360, 30)
                                          : Audio(entry.codec, "");
    EXPECT_TRUE(Query(provider, request).supported) << entry.settings_name;
  }
}

TEST(CapabilityProviderTest, UnknownCodecIsNotSupported) {
  const DesktopCapabilityProvider provider = MakeProvider();
  EXPECT_FALSE(
      Query(provider, Video(CodecId::kUnknown, "", 1280, 720, 30)).supported);
  EXPECT_FALSE(
      Query(provider, Video(CodecId::kUnknown, "xyz1.2.3", 1280, 720, 30))
          .supported);
  EXPECT_FALSE(
      Query(provider, Video(CodecId::kH264, "vp09.00.10.08", 1280, 720, 30))
          .supported);
}

TEST(CapabilityProviderTest, UnsupportedProfilesAreRejected) {
  const DesktopCapabilityProvider provider = MakeProvider();
  EXPECT_FALSE(
      Query(provider, Video(CodecId::kH264, "avc1.7f001f", 1280, 720, 30))
          .supported);
  EXPECT_FALSE(
      Query(provider, Video(CodecId::kVp9, "vp09.00.10.10", 1280, 720, 30))
          .supported);
  EXPECT_FALSE(
      Query(provider, Video(CodecId::kAv1, "av01.3.08M.08", 1280, 720, 30))
          .supported);
  EXPECT_TRUE(
      Query(provider, Video(CodecId::kH264, "avc1.6e001f", 1280, 720, 30))
          .supported);
}

TEST(CapabilityProviderTest, MimeTypeMustNameAKnownContainer) {
  const DesktopCapabilityProvider provider = MakeProvider();
  CapabilityRequest request = Video(CodecId::kH264, "", 1280, 720, 30);
  request.mime_type = "Video/MP4; codecs=\"avc1.64001f\"";
  EXPECT_TRUE(Query(provider, request).supported);
  request.mime_type = "";
  EXPECT_TRUE(Query(provider, request).supported);
  request.mime_type = "video/x-unknown";
  EXPECT_FALSE(Query(provider, request).supported);
}

TEST(CapabilityProviderTest, MissingDecoderIsNotSupported) {
  auto probe = FakeProbe::WithEveryTableDecoder();
  probe->available.erase("dav1d");
  const DesktopCapabilityProvider provider = MakeProvider({}, probe);
  EXPECT_FALSE(
      Query(provider, Video(CodecId::kAv1, "", 1280, 720, 30)).supported);
  EXPECT_TRUE(
      Query(provider, Video(CodecId::kH264, "", 1280, 720, 30)).supported);
}

TEST(CapabilityProviderTest, DisablingADecoderChangesTheAnswer) {
  const CapabilityRequest request = Video(CodecId::kHevc, "", 1920, 1080, 30);
  EXPECT_TRUE(Query(MakeProvider(), request).supported);
  DecodeLimits limits;
  limits.disabled_decoders = {"hevc"};
  const DesktopCapabilityProvider provider = MakeProvider(limits);
  EXPECT_FALSE(Query(provider, request).supported);
  EXPECT_TRUE(
      Query(provider, Video(CodecId::kH264, "", 1920, 1080, 30)).supported);
}

TEST(CapabilityProviderTest, MaximumVideoHeightLimitsVideoOnly) {
  DecodeLimits limits;
  limits.max_video_height = 1080;
  const DesktopCapabilityProvider provider = MakeProvider(limits);
  EXPECT_TRUE(
      Query(provider, Video(CodecId::kH264, "", 1920, 1080, 30)).supported);
  EXPECT_FALSE(
      Query(provider, Video(CodecId::kH264, "", 3840, 2160, 30)).supported);
  EXPECT_TRUE(Query(provider, Audio(CodecId::kAac, "")).supported);
}

TEST(CapabilityProviderTest, SmoothnessAccountsForFrameRate) {
  const DesktopCapabilityProvider provider = MakeProvider();
  EXPECT_TRUE(
      Query(provider, Video(CodecId::kH264, "", 1920, 1080, 60)).smooth);
  EXPECT_TRUE(
      Query(provider, Video(CodecId::kH264, "", 1920, 1080, 144)).smooth);
  EXPECT_TRUE(
      Query(provider, Video(CodecId::kH264, "", 2560, 1440, 120)).smooth);
  EXPECT_FALSE(
      Query(provider, Video(CodecId::kH264, "", 2560, 1440, 240)).smooth);
  EXPECT_TRUE(
      Query(provider, Video(CodecId::kH264, "", 3840, 2160, 60)).smooth);
  EXPECT_FALSE(
      Query(provider, Video(CodecId::kH264, "", 3840, 2160, 120)).smooth);
}

TEST(CapabilityProviderTest, SmoothnessDiffersByCodec) {
  const DesktopCapabilityProvider provider = MakeProvider();
  const DecodingCapability av1 =
      Query(provider, Video(CodecId::kAv1, "", 3840, 2160, 60));
  EXPECT_TRUE(av1.supported);
  EXPECT_FALSE(av1.smooth);
  EXPECT_TRUE(Query(provider, Video(CodecId::kAv1, "", 1920, 1080, 60)).smooth);
  EXPECT_TRUE(
      Query(provider, Video(CodecId::kHevc, "", 3840, 2160, 30)).smooth);
  EXPECT_FALSE(
      Query(provider, Video(CodecId::kHevc, "", 3840, 2160, 60)).smooth);
}

TEST(CapabilityProviderTest, UnknownGeometryIsNotSmooth) {
  const DesktopCapabilityProvider provider = MakeProvider();
  const DecodingCapability result =
      Query(provider, Video(CodecId::kH264, "", 0, 0, 30));
  EXPECT_TRUE(result.supported);
  EXPECT_FALSE(result.smooth);
}

TEST(CapabilityProviderTest, UnknownFrameRateIsAssumedToBe60) {
  const DesktopCapabilityProvider provider = MakeProvider();
  CapabilityRequest request = Video(CodecId::kHevc, "", 3840, 2160, 30);
  std::get<VideoInfo>(request.stream.format).frame_rate = Rational();
  EXPECT_FALSE(Query(provider, request).smooth);
  std::get<VideoInfo>(request.stream.format).display_width = 1280;
  std::get<VideoInfo>(request.stream.format).coded_width = 1280;
  std::get<VideoInfo>(request.stream.format).coded_height = 720;
  EXPECT_TRUE(Query(provider, request).smooth);
}

TEST(CapabilityProviderTest, SoftwareDecodingIsNeverPowerEfficient) {
  const DesktopCapabilityProvider provider = MakeProvider();
  const DecodingCapability result =
      Query(provider, Video(CodecId::kH264, "", 640, 360, 24));
  EXPECT_FALSE(result.power_efficient);
  EXPECT_EQ(result.preferred_output_mode, OutputMode::kTexture);
  EXPECT_FALSE(result.accepts_fragments);
}

TEST(CapabilityProviderTest, AudioIsAlwaysSmooth) {
  const DesktopCapabilityProvider provider = MakeProvider();
  EXPECT_TRUE(Query(provider, Audio(CodecId::kOpus, "opus")).smooth);
}

TEST(CapabilityProviderTest, AudioCodecOnAVideoStreamIsNotSupported) {
  const DesktopCapabilityProvider provider = MakeProvider();
  EXPECT_FALSE(Query(provider, Video(CodecId::kAac, "mp4a.40.2", 1280, 720, 30))
                   .supported);
  EXPECT_FALSE(Query(provider, Audio(CodecId::kH264, "avc1.64001f")).supported);
}

TEST(CapabilityProviderTest, TextStreamIsNotSupported) {
  const DesktopCapabilityProvider provider = MakeProvider();
  CapabilityRequest request;
  request.stream.format = TextInfo{};
  EXPECT_FALSE(Query(provider, request).supported);
}

TEST(CapabilityProviderTest, HdrKeySystemAndEncryptionAreNotSupportedYet) {
  const DesktopCapabilityProvider provider = MakeProvider();
  CapabilityRequest request = Video(CodecId::kHevc, "", 1920, 1080, 30);
  request.hdr_format = "smpteSt2084";
  EXPECT_FALSE(Query(provider, request).supported);
  request.hdr_format.clear();
  request.key_system = "com.widevine.alpha";
  EXPECT_FALSE(Query(provider, request).supported);
  request.key_system.clear();
  request.encryption_scheme = EncryptionScheme::kCenc;
  EXPECT_FALSE(Query(provider, request).supported);
}

TEST(CapabilityProviderTest, InvalidRequestsAreRejected) {
  const DesktopCapabilityProvider provider = MakeProvider();
  CapabilityRequest request = Video(CodecId::kH264, "", 1920, 1080, 30);
  request.bitrate = -1;
  EXPECT_EQ(provider.QueryDecoding(request).status().code(),
            absl::StatusCode::kInvalidArgument);
  request = Video(CodecId::kH264, "", -1, 1080, 30);
  EXPECT_EQ(provider.QueryDecoding(request).status().code(),
            absl::StatusCode::kInvalidArgument);
  request = Video(CodecId::kH264, "", 1920, 1080, -30);
  EXPECT_EQ(provider.QueryDecoding(request).status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(CapabilityProviderTest, RepeatedQueriesHitTheCache) {
  auto probe = FakeProbe::WithEveryTableDecoder();
  const DesktopCapabilityProvider provider = MakeProvider({}, probe);
  const CapabilityRequest request = Video(CodecId::kH264, "", 1920, 1080, 30);
  const DecodingCapability first = Query(provider, request);
  EXPECT_EQ(probe->calls, 1);
  for (int i = 0; i < 100; ++i) {
    const DecodingCapability again = Query(provider, request);
    EXPECT_EQ(again.supported, first.supported);
    EXPECT_EQ(again.smooth, first.smooth);
  }
  EXPECT_EQ(probe->calls, 1);
  Query(provider, Video(CodecId::kH264, "", 1920, 1080, 60));
  EXPECT_EQ(probe->calls, 2);
}

TEST(CapabilityProviderTest, CacheStaysBounded) {
  auto probe = FakeProbe::WithEveryTableDecoder();
  const DesktopCapabilityProvider provider = MakeProvider({}, probe);
  for (int32_t width = 1; width <= 2000; ++width) {
    Query(provider, Video(CodecId::kH264, "", width, 720, 30));
  }
  EXPECT_EQ(probe->calls, 2000);
}

TEST(CapabilityProviderTest, ProbeFailureIsReportedAndNotCached) {
  auto probe = FakeProbe::WithEveryTableDecoder();
  probe->failure = absl::UnavailableError("probe failed");
  const DesktopCapabilityProvider provider = MakeProvider({}, probe);
  const CapabilityRequest request = Video(CodecId::kH264, "", 1920, 1080, 30);
  EXPECT_EQ(provider.QueryDecoding(request).status().code(),
            absl::StatusCode::kUnavailable);
  probe->failure.reset();
  EXPECT_TRUE(Query(provider, request).supported);
}

TEST(CapabilityProviderTest, ChangeTypeAlwaysRecreates) {
  const DesktopCapabilityProvider provider = MakeProvider();
  const auto result = provider.CanChangeType(StreamConfig{}, StreamConfig{});
  ASSERT_TRUE(result.ok());
  EXPECT_FALSE(*result);
}

void ExpectParsed(std::string_view text, CodecId codec, int profile, int level,
                  int bit_depth) {
  const std::optional<ParsedCodec> result = ParseCodecString(text);
  ASSERT_TRUE(result.has_value()) << text;
  if (!result.has_value()) {
    return;
  }
  const ParsedCodec parsed = *result;
  EXPECT_EQ(parsed.codec, codec) << text;
  EXPECT_EQ(parsed.profile, profile) << text;
  EXPECT_EQ(parsed.level, level) << text;
  EXPECT_EQ(parsed.bit_depth, bit_depth) << text;
}

TEST(CodecStringTest, ParsesVideoFamilies) {
  ExpectParsed("avc1.64001f", CodecId::kH264, 100, 31, 0);
  ExpectParsed("avc3.42E01E", CodecId::kH264, 66, 30, 0);
  ExpectParsed("avc1", CodecId::kH264, -1, -1, 0);
  ExpectParsed("hvc1.1.6.L93.B0", CodecId::kHevc, 1, 93, 0);
  ExpectParsed("hev1.A2.4.H120.B0", CodecId::kHevc, 2, 120, 0);
  ExpectParsed("vp09.02.10.10", CodecId::kVp9, 2, 10, 10);
  ExpectParsed("av01.0.08M.10", CodecId::kAv1, 0, 8, 10);
}

TEST(CodecStringTest, ParsesAudioFamilies) {
  ExpectParsed("mp4a.40.2", CodecId::kAac, 2, -1, 0);
  ExpectParsed("mp4a.6B", CodecId::kMp3, -1, -1, 0);
  ExpectParsed("mp4a.A5", CodecId::kAc3, -1, -1, 0);
  ExpectParsed("mp4a.A6", CodecId::kEac3, -1, -1, 0);
  ExpectParsed("opus", CodecId::kOpus, -1, -1, 0);
  ExpectParsed("ac-3", CodecId::kAc3, -1, -1, 0);
  ExpectParsed("ec-3", CodecId::kEac3, -1, -1, 0);
  ExpectParsed("flac", CodecId::kFlac, -1, -1, 0);
  ExpectParsed("mp3", CodecId::kMp3, -1, -1, 0);
}

TEST(CodecStringTest, RejectsMalformedStrings) {
  const auto malformed =
      std::to_array<std::string_view>({"",
                                       ".",
                                       "avc1.",
                                       "avc1.64001",
                                       "avc1.zz001f",
                                       "avc1.64001f.1",
                                       "hvc1.1.6",
                                       "hvc1.1.6.X93.B0",
                                       "vp09.00",
                                       "vp09.aa.10.08",
                                       "av01.0.08.08",
                                       "av01.0.08X.08",
                                       "mp4a",
                                       "mp4a.40.x",
                                       "mp4a.zz",
                                       "mp4a.FF",
                                       "opus.1",
                                       "xyz",
                                       "mp3.1",
                                       "vp09.0000000.10.08"});
  for (const std::string_view text : malformed) {
    EXPECT_FALSE(ParseCodecString(text).has_value()) << text;
  }
}

void ExpectDecoderOpens(std::string_view decoder_name) {
  if (decoder_name == "dav1d") {
    Dav1dSettings settings;
    dav1d_default_settings(&settings);
    std::array<Dav1dContext*, 1> context{};
    ASSERT_EQ(dav1d_open(context.data(), &settings), 0);
    dav1d_close(context.data());
    return;
  }
  const std::string name(decoder_name);
  const AVCodec* codec = avcodec_find_decoder_by_name(name.c_str());
  ASSERT_NE(codec, nullptr) << name;
  AVCodecContext* context = avcodec_alloc_context3(codec);
  ASSERT_NE(context, nullptr);
  EXPECT_EQ(avcodec_open2(context, codec, nullptr), 0) << name;
  avcodec_free_context(&context);
}

TEST(SoftwareDecoderProbeTest, EveryTableDecoderCanBeOpened) {
  const SoftwareDecoderProbe probe;
  for (const DecoderEntry& entry : SoftwareDecoderTable()) {
    const auto available = probe.IsCompiledIn(entry.decoder_name);
    ASSERT_TRUE(available.ok()) << entry.decoder_name;
    ASSERT_TRUE(*available) << entry.decoder_name;
    ExpectDecoderOpens(entry.decoder_name);
  }
}

TEST(SoftwareDecoderProbeTest, UnknownDecoderIsAbsent) {
  const SoftwareDecoderProbe probe;
  const auto available = probe.IsCompiledIn("no-such-decoder");
  ASSERT_TRUE(available.ok());
  EXPECT_FALSE(*available);
}

TEST(SoftwareDecoderProbeTest, RealProviderSupportsEveryM1Codec) {
  const DesktopCapabilityProvider provider(
      DecodeLimits{}, std::make_shared<SoftwareDecoderProbe>());
  for (const DecoderEntry& entry : SoftwareDecoderTable()) {
    const CapabilityRequest request =
        entry.smooth_luma_samples_per_second > 0
            ? Video(entry.codec, "", 1280, 720, 30)
            : Audio(entry.codec, "");
    EXPECT_TRUE(Query(provider, request).supported) << entry.settings_name;
  }
}

}
}
