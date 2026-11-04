#include "umbra/player/source_router.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/statusor.h"
#include "fake_http_transport.h"
#include "fake_stream_provider.h"
#include "gtest/gtest.h"
#include "mkv_builder.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/http_transport.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/stream_info.h"
#include "ts_builder.h"
#include "umbra/base/error_detail.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/test_support/test_task_environment.h"
#include "umbra/base/url_redactor.h"
#include "umbra/manifest/dash_mpd_parser.h"
#include "umbra/manifest/hls_parser.h"
#include "umbra/manifest/ids.h"
#include "umbra/manifest/presentation.h"
#include "umbra/pipeline/sample_source.h"
#include "umbra/player/text_track.h"
#include "umbra/player/text_track_routing.h"
#include "umbra/source/scheme_registry.h"
#include "umbra/source/source_resolver.h"

namespace umbra::player::internal {
namespace {

class RecordingSink final : public pipeline::SampleSink {
 public:
  void OnOpened(pipeline::SourceDescription value) override {
    description = std::move(value);
  }
  void OnOpenFailed(base::ErrorDetail value) override {
    error = std::move(value);
  }
  void OnDurationChanged(penumbra::MediaTime duration) override {
    static_cast<void>(duration);
  }
  void OnSamples(penumbra::SeekTicket ticket, uint32_t track_id,
                 std::vector<penumbra::EncodedSample> samples) override {
    static_cast<void>(ticket);
    static_cast<void>(track_id);
    static_cast<void>(samples);
  }
  void OnEndOfStream(penumbra::SeekTicket ticket, uint32_t track_id) override {
    static_cast<void>(ticket);
    static_cast<void>(track_id);
  }
  void OnSourceError(base::ErrorDetail value) override {
    error = std::move(value);
  }

  std::optional<pipeline::SourceDescription> description;
  std::optional<base::ErrorDetail> error;
};

class SourceRouterTest : public ::testing::Test {
 protected:
  static TextTrackRoutingOptions DefaultTextOptions() {
    TextTrackRoutingOptions options;
    options.cue_memory_budget_bytes = size_t{1024} * 1024;
    return options;
  }

  SourceRouterTest()
      : runner_(environment_.CreateSequencedTaskRunner(
            base::TaskPriority::kControl)),
        redactor_(std::make_shared<const base::UrlRedactor>()) {}

  void VerifyAdaptiveRoute(const std::string& manifest,
                           const std::string& url) {
    const std::string path =
        std::string(ALUMBRA_ROUTER_FIXTURE_DIR) + "/" + manifest;
    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file),
                               std::istreambuf_iterator<char>()};
    provider_ = std::make_shared<source::test::FakeStreamProvider>(
        std::move(bytes), true, true, 4096);
    transport_ = std::make_shared<source::test::FakeHttpTransport>();
    auto registry = std::make_shared<source::SchemeRegistry>();
    ASSERT_TRUE(registry->RegisterByteStream(provider_).ok());
    auto resolver =
        std::make_shared<source::SourceResolver>(registry, redactor_);
    auto create = CreateSourceResolver(
        resolver, runner_,
        std::static_pointer_cast<penumbra::HttpTransport>(transport_),
        redactor_);
    const std::unique_ptr<pipeline::SampleSource> routed =
        create(url, nullptr, DefaultTextOptions(), {}, false);
    ASSERT_NE(routed, nullptr);
    auto sink = std::make_shared<RecordingSink>();
    ASSERT_TRUE(runner_->PostTask(
        [source = routed.get(), sink] { source->Open(sink); }));
    environment_.RunUntilIdle();
    for (size_t i = 0; i < 20 && transport_->Requests().empty(); ++i) {
      provider_->StepAll();
      environment_.RunUntilIdle();
    }
    EXPECT_FALSE(sink->error.has_value());
    EXPECT_FALSE(transport_->Requests().empty());
  }

  base::TestTaskEnvironment environment_;
  std::shared_ptr<base::SequencedTaskRunner> runner_;
  std::shared_ptr<const base::UrlRedactor> redactor_;
  std::shared_ptr<source::test::FakeStreamProvider> provider_;
  std::shared_ptr<source::test::FakeHttpTransport> transport_;
};

TEST_F(SourceRouterTest, RoutesHlsManifestToAdaptiveFetching) {
  VerifyAdaptiveRoute("apple_low_latency_media.m3u8",
                      "fake://example.test/playlist.m3u8");
}

TEST_F(SourceRouterTest, RoutesDashManifestToAdaptiveFetching) {
  VerifyAdaptiveRoute("list-and-sidx.mpd", "fake://example.test/manifest.mpd");
}

TEST_F(SourceRouterTest, LoadsWrongExtensionTextUrlByPayloadSignature) {
  const std::string text = "1\n00:00:01,000 --> 00:00:02,000\nA subtitle\n";
  provider_ = std::make_shared<source::test::FakeStreamProvider>(
      std::vector<uint8_t>(text.begin(), text.end()), true, true, 4096);
  auto registry = std::make_shared<source::SchemeRegistry>();
  ASSERT_TRUE(registry->RegisterByteStream(provider_).ok());
  auto resolver = std::make_shared<source::SourceResolver>(registry, redactor_);
  auto load = CreateTextTrackUrlLoader(resolver, runner_, redactor_);
  auto track = std::make_shared<TextTrack>(TextTrackKind::kSubtitles, "English",
                                           "en", 4096);
  bool completed = false;
  bool loaded = false;
  ASSERT_TRUE(runner_->PostTask([&] {
    load("fake://example.test/subtitle.bin", 4096,
         [track, &completed, &loaded](absl::StatusOr<std::string> result) {
           completed = true;
           loaded = result.ok() &&
                    LoadTextTrackPayload(*track, *result, "windows-1252", true);
         });
  }));
  environment_.RunUntilIdle();
  provider_->StepAll();
  environment_.RunUntilIdle();
  ASSERT_TRUE(completed);
  ASSERT_TRUE(loaded);
  ASSERT_EQ(track->cues().size(), 1U);
  EXPECT_EQ(track->cues().front().runs.front().text, "A subtitle");
}

TEST_F(SourceRouterTest, DiscoversHlsSubtitleRenditionMetadata) {
  std::ifstream input(std::string(ALUMBRA_ROUTER_FIXTURE_DIR) +
                      "/apple_multivariant.m3u8");
  const std::string playlist{std::istreambuf_iterator<char>(input),
                             std::istreambuf_iterator<char>()};
  auto parsed = manifest::ParseHlsPlaylist(
      playlist, {.base_uri = "https://media.example.test/master.m3u8",
                 .query_parameters = {},
                 .imported_variables = {},
                 .strict = false});
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  const auto found = std::ranges::find_if(
      parsed->hls->renditions, [](const manifest::HlsRendition& rendition) {
        return rendition.type == manifest::RenditionType::kSubtitles;
      });
  ASSERT_NE(found, parsed->hls->renditions.end());
  const TextTrackMetadata metadata = MetadataForHlsTextRendition(*found);
  EXPECT_EQ(metadata.kind, TextTrackKind::kSubtitles);
  EXPECT_EQ(metadata.language, "en");
  EXPECT_EQ(metadata.label, "English");
}

TEST_F(SourceRouterTest, DiscoversDashTextAdaptationSetMetadata) {
  constexpr std::string_view mpd =
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\" type=\"static\" "
      "mediaPresentationDuration=\"PT10S\"><Period><AdaptationSet "
      "id=\"text\" contentType=\"text\" mimeType=\"application/mp4\" "
      "lang=\"en\"><Label>English captions</Label><Role "
      "schemeIdUri=\"urn:mpeg:dash:role:2011\" value=\"caption\"/>"
      "<Representation id=\"wvtt\" codecs=\"wvtt\"/></AdaptationSet>"
      "</Period></MPD>";
  auto parsed = manifest::ParseDashMpd(mpd);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  const auto& switching_set = *parsed->periods.front()->switching_sets.front();
  EXPECT_EQ(switching_set.type, penumbra::StreamType::kText);
  const TextTrackMetadata metadata =
      MetadataForManifestTextTrack(switching_set);
  ASSERT_TRUE(std::holds_alternative<manifest::DashTrackExtension>(
      switching_set.tracks.front()->extension));
  EXPECT_EQ(std::get<manifest::DashTrackExtension>(
                switching_set.tracks.front()->extension)
                .text_codec,
            "wvtt");
  EXPECT_EQ(metadata.kind, TextTrackKind::kCaptions);
  EXPECT_EQ(metadata.language, "en");
  EXPECT_EQ(metadata.label, "English captions");
}

TEST_F(SourceRouterTest, AddsHlsTextRenditionsToPlayerTrackList) {
  const std::string path =
      std::string(ALUMBRA_ROUTER_FIXTURE_DIR) + "/apple_multivariant.m3u8";
  std::ifstream input(path, std::ios::binary);
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input),
                             std::istreambuf_iterator<char>()};
  provider_ = std::make_shared<source::test::FakeStreamProvider>(
      std::move(bytes), true, true, 4096);
  transport_ = std::make_shared<source::test::FakeHttpTransport>();
  auto registry = std::make_shared<source::SchemeRegistry>();
  ASSERT_TRUE(registry->RegisterByteStream(provider_).ok());
  auto resolver = std::make_shared<source::SourceResolver>(registry, redactor_);
  auto create = CreateSourceResolver(
      resolver, runner_,
      std::static_pointer_cast<penumbra::HttpTransport>(transport_), redactor_);
  TextTrackList tracks;
  auto routed = create("fake://example.test/master.m3u8", &tracks,
                       DefaultTextOptions(), {}, false);
  ASSERT_NE(routed, nullptr);
  auto sink = std::make_shared<RecordingSink>();
  ASSERT_TRUE(
      runner_->PostTask([source = routed.get(), sink] { source->Open(sink); }));
  environment_.RunUntilIdle();
  provider_->StepAll();
  environment_.RunUntilIdle();
  ASSERT_EQ(tracks.length(), 1U);
  ASSERT_NE(tracks.item(0), nullptr);
  EXPECT_EQ(tracks.item(0)->kind(), TextTrackKind::kSubtitles);
  EXPECT_EQ(tracks.item(0)->label(), "English");
  EXPECT_EQ(tracks.item(0)->language(), "en");
}

TEST_F(SourceRouterTest, DiscoversSiblingSubtitleFilesWhenEnabled) {
  std::ifstream input(std::string(ALUMBRA_PROGRESSIVE_FIXTURE_DIR) +
                          "/matroska_multi_codec.mkv",
                      std::ios::binary);
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input),
                             std::istreambuf_iterator<char>()};
  provider_ = std::make_shared<source::test::FakeStreamProvider>(
      std::move(bytes), true, true, 4096);
  provider_->SetSiblingFileUrls(
      {"file:///media/movie.srt", "file:///media/movie.eng.vtt"});
  transport_ = std::make_shared<source::test::FakeHttpTransport>();
  auto registry = std::make_shared<source::SchemeRegistry>();
  ASSERT_TRUE(registry->RegisterByteStream(provider_).ok());
  auto resolver = std::make_shared<source::SourceResolver>(registry, redactor_);
  auto loader = CreateTextTrackUrlLoader(resolver, runner_, redactor_);
  auto create = CreateSourceResolver(
      resolver, runner_,
      std::static_pointer_cast<penumbra::HttpTransport>(transport_), redactor_,
      loader);
  TextTrackList tracks;
  TextTrackRoutingOptions options;
  options.cue_memory_budget_bytes = size_t{1024} * 1024;
  options.max_payload_bytes = 1024;
  options.auto_discover_siblings = true;
  auto routed = create("file:///media/movie.mkv", &tracks, options, {}, false);
  ASSERT_NE(routed, nullptr);
  auto sink = std::make_shared<RecordingSink>();
  ASSERT_TRUE(
      runner_->PostTask([source = routed.get(), sink] { source->Open(sink); }));
  for (size_t attempt = 0; attempt < 20 && tracks.length() < 2; ++attempt) {
    environment_.RunUntilIdle();
    provider_->StepAll();
  }
  ASSERT_EQ(tracks.length(), 2U);
  EXPECT_EQ(tracks.item(0)->kind(), TextTrackKind::kSubtitles);
  EXPECT_EQ(tracks.item(1)->kind(), TextTrackKind::kSubtitles);
  EXPECT_EQ(tracks.item(0)->language() + tracks.item(1)->language(), "eng");
  ASSERT_TRUE(runner_->PostTask([source = routed.get()] { source->Close(); }));
  environment_.RunUntilIdle();
}

TEST_F(SourceRouterTest, DoesNotDiscoverSiblingFilesWhenDisabled) {
  std::ifstream input(std::string(ALUMBRA_PROGRESSIVE_FIXTURE_DIR) +
                          "/matroska_multi_codec.mkv",
                      std::ios::binary);
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input),
                             std::istreambuf_iterator<char>()};
  provider_ = std::make_shared<source::test::FakeStreamProvider>(
      std::move(bytes), true, true, 4096);
  provider_->SetSiblingFileUrls({"file:///media/movie.srt"});
  transport_ = std::make_shared<source::test::FakeHttpTransport>();
  auto registry = std::make_shared<source::SchemeRegistry>();
  ASSERT_TRUE(registry->RegisterByteStream(provider_).ok());
  auto resolver = std::make_shared<source::SourceResolver>(registry, redactor_);
  auto loader = CreateTextTrackUrlLoader(resolver, runner_, redactor_);
  auto create = CreateSourceResolver(
      resolver, runner_,
      std::static_pointer_cast<penumbra::HttpTransport>(transport_), redactor_,
      loader);
  TextTrackList tracks;
  TextTrackRoutingOptions options;
  options.cue_memory_budget_bytes = size_t{1024} * 1024;
  options.max_payload_bytes = 1024;
  options.auto_discover_siblings = false;
  auto routed = create("file:///media/movie.mkv", &tracks, options, {}, false);
  ASSERT_NE(routed, nullptr);
  auto sink = std::make_shared<RecordingSink>();
  ASSERT_TRUE(
      runner_->PostTask([source = routed.get(), sink] { source->Open(sink); }));
  for (size_t attempt = 0; attempt < 20 && !sink->description.has_value();
       ++attempt) {
    environment_.RunUntilIdle();
    provider_->StepAll();
  }
  EXPECT_EQ(tracks.length(), 0U);
  ASSERT_TRUE(runner_->PostTask([source = routed.get()] { source->Close(); }));
  environment_.RunUntilIdle();
}

TEST_F(SourceRouterTest, RoutesMatroskaSubtitleSamplesIntoTextCues) {
  constexpr std::string_view header =
      "[Script Info]\nPlayResX: 640\nPlayResY: 360\n[Events]\n"
      "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, "
      "Effect, Text\n";
  demux::test::MkvFileSpec spec;
  demux::test::MkvTrackSpec video;
  video.number = 1;
  video.codec = demux::test::MkvTestCodec::kVp9;
  demux::test::MkvTrackSpec subtitle;
  subtitle.number = 2;
  subtitle.codec = demux::test::MkvTestCodec::kAss;
  subtitle.codec_private.assign(header.begin(), header.end());
  spec.tracks = {video, subtitle};
  demux::test::MkvBlockSpec video_block;
  video_block.track = 1;
  video_block.frames = {demux::test::FrameBytes(1, 0, 16)};
  demux::test::MkvBlockSpec subtitle_block;
  subtitle_block.track = 2;
  subtitle_block.group = true;
  subtitle_block.duration = 2000;
  const std::string event = "0,Default,,0,0,0,,Matroska subtitle";
  subtitle_block.frames.emplace_back(event.begin(), event.end());
  demux::test::MkvClusterSpec cluster;
  cluster.blocks = {video_block, subtitle_block};
  spec.clusters = {cluster};
  std::vector<uint8_t> bytes = demux::test::BuildMkv(spec).bytes;
  provider_ = std::make_shared<source::test::FakeStreamProvider>(
      std::move(bytes), true, true, 4096);
  transport_ = std::make_shared<source::test::FakeHttpTransport>();
  auto registry = std::make_shared<source::SchemeRegistry>();
  ASSERT_TRUE(registry->RegisterByteStream(provider_).ok());
  auto resolver = std::make_shared<source::SourceResolver>(registry, redactor_);
  auto loader = CreateTextTrackUrlLoader(resolver, runner_, redactor_);
  auto create = CreateSourceResolver(
      resolver, runner_,
      std::static_pointer_cast<penumbra::HttpTransport>(transport_), redactor_,
      loader);
  TextTrackList tracks;
  auto routed = create("file:///media/movie.mkv", &tracks, DefaultTextOptions(),
                       {}, false);
  ASSERT_NE(routed, nullptr);
  auto sink = std::make_shared<RecordingSink>();
  ASSERT_TRUE(
      runner_->PostTask([source = routed.get(), sink] { source->Open(sink); }));
  for (size_t attempt = 0; attempt < 30 && sink->description == std::nullopt;
       ++attempt) {
    provider_->StepAll();
    environment_.RunUntilIdle();
  }
  ASSERT_TRUE(sink->description.has_value());
  ASSERT_EQ(tracks.length(), 1U);
  ASSERT_EQ(sink->description->streams.size(), 1U);
  ASSERT_TRUE(runner_->PostTask(
      [source = routed.get()] { source->RequestSamples(1, 1, 10); }));
  for (size_t attempt = 0; attempt < 30 && tracks.item(0)->cues().empty();
       ++attempt) {
    provider_->StepAll();
    environment_.RunUntilIdle();
  }
  ASSERT_EQ(tracks.item(0)->kind(), TextTrackKind::kSubtitles);
  ASSERT_EQ(tracks.item(0)->language(), "eng");
  ASSERT_EQ(tracks.item(0)->cues().size(), 1U);
  EXPECT_EQ(tracks.item(0)->cues().front().runs.front().text,
            "Matroska subtitle");
  ASSERT_TRUE(runner_->PostTask([source = routed.get()] { source->Close(); }));
  environment_.RunUntilIdle();
}

TEST_F(SourceRouterTest, ExposesInBandCeaCaptionsAsCaptionTracks) {
  demux::test::SyntheticSpec spec;
  spec.frames = 10;
  spec.audio = false;
  spec.captions = true;
  spec.caption_data = {0xFC, 0x14, 0x20, 0xFC, 0x14, 0x20, 0xFC,
                       0x48, 0x49, 0xFC, 0x14, 0x2F, 0xFC, 0x14,
                       0x2F, 0xFF, 0x02, 0x21, 0xFE, 0x41, 0x00};
  const demux::test::Synthetic synthetic = demux::test::BuildSynthetic(spec);
  const std::vector<uint8_t> bytes = demux::test::Flatten(synthetic.packets);
  provider_ = std::make_shared<source::test::FakeStreamProvider>(bytes, true,
                                                                 true, 4096);
  transport_ = std::make_shared<source::test::FakeHttpTransport>();
  auto registry = std::make_shared<source::SchemeRegistry>();
  ASSERT_TRUE(registry->RegisterByteStream(provider_).ok());
  auto resolver = std::make_shared<source::SourceResolver>(registry, redactor_);
  auto create = CreateSourceResolver(
      resolver, runner_,
      std::static_pointer_cast<penumbra::HttpTransport>(transport_), redactor_);
  TextTrackList tracks;
  auto routed = create("fake://example.test/captions.ts", &tracks,
                       DefaultTextOptions(), {}, false);
  ASSERT_NE(routed, nullptr);
  auto sink = std::make_shared<RecordingSink>();
  ASSERT_TRUE(
      runner_->PostTask([source = routed.get(), sink] { source->Open(sink); }));
  for (size_t attempt = 0; attempt < 30 && !sink->description.has_value();
       ++attempt) {
    provider_->StepAll();
    environment_.RunUntilIdle();
  }
  ASSERT_TRUE(sink->description.has_value());
  ASSERT_TRUE(runner_->PostTask(
      [source = routed.get()] { source->RequestSamples(1, 0x101, 100); }));
  for (size_t attempt = 0; attempt < 30 && tracks.length() == 0; ++attempt) {
    provider_->StepAll();
    environment_.RunUntilIdle();
  }
  ASSERT_EQ(tracks.length(), 2U);
  std::map<std::string, std::shared_ptr<TextTrack>> captions;
  for (size_t index = 0; index < tracks.length(); ++index) {
    const std::shared_ptr<TextTrack> track = tracks.item(index);
    ASSERT_EQ(track->kind(), TextTrackKind::kCaptions);
    EXPECT_TRUE(track->language().empty());
    captions.emplace(track->label(), track);
  }
  ASSERT_TRUE(captions.contains("CC1"));
  ASSERT_TRUE(captions.contains("SERVICE1"));
  ASSERT_EQ(captions.at("CC1")->cues().size(), 1U);
  EXPECT_EQ(captions.at("CC1")->cues().front().runs.front().text, "HI");
  ASSERT_EQ(captions.at("SERVICE1")->cues().size(), 1U);
  EXPECT_EQ(captions.at("SERVICE1")->cues().front().runs.front().text, "A");
  ASSERT_TRUE(runner_->PostTask([source = routed.get()] { source->Close(); }));
  environment_.RunUntilIdle();
}

TEST_F(SourceRouterTest, LoadsHlsTextRenditionCues) {
  std::ifstream input(std::string(ALUMBRA_ROUTER_FIXTURE_DIR) +
                      "/apple_multivariant.m3u8");
  const std::string master{std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>()};
  provider_ = std::make_shared<source::test::FakeStreamProvider>(
      std::vector<uint8_t>(master.begin(), master.end()), true, true, 4096);
  transport_ = std::make_shared<source::test::FakeHttpTransport>();
  auto registry = std::make_shared<source::SchemeRegistry>();
  ASSERT_TRUE(registry->RegisterByteStream(provider_).ok());
  auto resolver = std::make_shared<source::SourceResolver>(registry, redactor_);
  TextTrackUrlLoader loader = [](std::string url, size_t,
                                 TextPayloadCallback callback) {
    if (url.find("subs/en.m3u8") != std::string::npos) {
      callback(
          std::string("#EXTM3U\n#EXT-X-TARGETDURATION:2\n"
                      "#EXTINF:2.0,\nsubtitle.vtt\n#EXT-X-ENDLIST\n"));
      return;
    }
    callback(
        std::string("WEBVTT\n\n00:00:00.500 --> 00:00:01.500\n"
                    "HLS cue\n"));
  };
  auto create = CreateSourceResolver(
      resolver, runner_,
      std::static_pointer_cast<penumbra::HttpTransport>(transport_), redactor_,
      loader);
  TextTrackList tracks;
  auto routed = create("fake://example.test/master.m3u8", &tracks,
                       DefaultTextOptions(), {}, false);
  ASSERT_NE(routed, nullptr);
  auto sink = std::make_shared<RecordingSink>();
  ASSERT_TRUE(
      runner_->PostTask([source = routed.get(), sink] { source->Open(sink); }));
  environment_.RunUntilIdle();
  provider_->StepAll();
  environment_.RunUntilIdle();
  ASSERT_EQ(tracks.length(), 1U);
  ASSERT_EQ(tracks.item(0)->language(), "en");
  ASSERT_EQ(tracks.item(0)->cues().size(), 1U);
  EXPECT_EQ(tracks.item(0)->cues().front().runs.front().text, "HLS cue");
  EXPECT_EQ(tracks.item(0)->cues().front().start_time, 0.5);
  ASSERT_TRUE(runner_->PostTask([source = routed.get()] { source->Close(); }));
  environment_.RunUntilIdle();
}

TEST_F(SourceRouterTest, LoadsDashTextAdaptationSetCues) {
  const std::string mpd =
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\" type=\"static\" "
      "mediaPresentationDuration=\"PT2S\"><Period duration=\"PT2S\">"
      "<AdaptationSet id=\"subtitles\" contentType=\"text\" "
      "mimeType=\"text/vtt\" lang=\"en\"><Label>English</Label>"
      "<Representation id=\"en\"><SegmentTemplate timescale=\"1000\" "
      "duration=\"2000\" endNumber=\"1\" "
      "media=\"subtitle-$Number$.vtt\"/></Representation>"
      "</AdaptationSet></Period></MPD>";
  provider_ = std::make_shared<source::test::FakeStreamProvider>(
      std::vector<uint8_t>(mpd.begin(), mpd.end()), true, true, 4096);
  transport_ = std::make_shared<source::test::FakeHttpTransport>();
  auto registry = std::make_shared<source::SchemeRegistry>();
  ASSERT_TRUE(registry->RegisterByteStream(provider_).ok());
  auto resolver = std::make_shared<source::SourceResolver>(registry, redactor_);
  TextTrackUrlLoader loader = [](std::string, size_t,
                                 TextPayloadCallback callback) {
    callback(
        std::string("WEBVTT\n\n00:00:00.250 --> 00:00:01.250\n"
                    "DASH cue\n"));
  };
  auto create = CreateSourceResolver(
      resolver, runner_,
      std::static_pointer_cast<penumbra::HttpTransport>(transport_), redactor_,
      loader);
  TextTrackList tracks;
  auto routed = create("fake://example.test/manifest.mpd", &tracks,
                       DefaultTextOptions(), {}, false);
  ASSERT_NE(routed, nullptr);
  auto sink = std::make_shared<RecordingSink>();
  ASSERT_TRUE(
      runner_->PostTask([source = routed.get(), sink] { source->Open(sink); }));
  environment_.RunUntilIdle();
  provider_->StepAll();
  environment_.RunUntilIdle();
  ASSERT_EQ(tracks.length(), 1U);
  EXPECT_EQ(tracks.item(0)->kind(), TextTrackKind::kSubtitles);
  EXPECT_EQ(tracks.item(0)->language(), "en");
  ASSERT_EQ(tracks.item(0)->cues().size(), 1U);
  EXPECT_EQ(tracks.item(0)->cues().front().runs.front().text, "DASH cue");
  ASSERT_TRUE(runner_->PostTask([source = routed.get()] { source->Close(); }));
  environment_.RunUntilIdle();
}

}
}
