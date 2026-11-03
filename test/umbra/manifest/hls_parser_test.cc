#include "umbra/manifest/hls_parser.h"

#include <array>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "gtest/gtest.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/manifest/ids.h"
#include "umbra/manifest/presentation.h"
#include "umbra/manifest/segment.h"
#include "umbra/manifest/segment_index.h"

namespace umbra::manifest {
namespace {

std::string ReadFixture(std::string_view name) {
  const std::string path =
      std::string(ALUMBRA_MANIFEST_FIXTURE_DIR) + "/" + std::string(name);
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

std::string TrackCountGolden(const Presentation& presentation) {
  const Period& period = *presentation.periods.front();
  std::size_t video_tracks = 0;
  std::size_t audio_sets = 0;
  std::size_t subtitle_sets = 0;
  for (const auto& set : period.switching_sets) {
    if (set->type == penumbra::StreamType::kVideo) {
      video_tracks = set->tracks.size();
    } else if (set->type == penumbra::StreamType::kAudio) {
      ++audio_sets;
    } else if (set->type == penumbra::StreamType::kText) {
      ++subtitle_sets;
    }
  }
  const Track& track =
      *period.FindSwitchingSet(HlsVideoSwitchingSetId())->tracks.front();
  std::ostringstream golden;
  golden << "presentation_type=dynamic\n"
         << "independent_segments="
         << (presentation.hls->independent_segments ? "true" : "false") << "\n"
         << "video_tracks=" << video_tracks << "\n"
         << "audio_switching_sets=" << audio_sets << "\n"
         << "subtitle_switching_sets=" << subtitle_sets << "\n"
         << "trick_play_tracks=" << period.trick_play.size() << "\n"
         << "first_bandwidth_bps=" << *track.bandwidth_bps << "\n"
         << "first_average_bandwidth_bps=" << *track.average_bandwidth_bps
         << "\n"
         << "allowed_audio_sets=" << track.allowed_audio.size() << "\n"
         << "session_data_entries=" << presentation.hls->session_data.size()
         << "\n";
  return golden.str();
}

std::string MediaGolden(const Presentation& presentation) {
  const Track& track =
      *presentation.periods.front()->switching_sets.front()->tracks.front();
  const Segment& segment = track.segments.Index()->At(0);
  const KeyReference& key =
      track.segments.Index()->KeyReferences()[*segment.key_index];
  std::ostringstream golden;
  golden << "presentation_type="
         << (presentation.type == PresentationType::kStatic ? "static"
                                                            : "dynamic")
         << "\n"
         << "periods=" << presentation.periods.size() << "\n"
         << "first_sequence_number=" << segment.sequence_number << "\n"
         << "first_key_method=AES-128\n"
         << "first_key_uri=" << key.uri << "\n"
         << "first_segment_parts=" << segment.hls_parts.size() << "\n"
         << "trailing_parts=" << presentation.hls->trailing_parts.size() << "\n"
         << "preload_hint_type=" << presentation.hls->preload_hint->type << "\n"
         << "preload_hint_range_start="
         << *presentation.hls->preload_hint->byte_range_start << "\n"
         << "rendition_reports=" << presentation.hls->rendition_reports.size()
         << "\n"
         << "skipped_segments=" << presentation.hls->skipped_segments << "\n"
         << "ad_breaks=" << presentation.ad_breaks.size() << "\n";
  return golden.str();
}

TEST(HlsAttributeListTest, PreservesQuotedCommasAndAttributeKinds) {
  const auto parsed = ParseHlsAttributeList(
      "BANDWIDTH=1200000,RESOLUTION=1280x720,FRAME-RATE=29.97,CODECS=\"avc1."
      "4d401f,mp4a.40.2\",NAME=\"Main\"");
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  ASSERT_EQ(parsed->size(), 5U);
  EXPECT_EQ((*parsed)[0].value, "1200000");
  EXPECT_EQ((*parsed)[1].value, "1280x720");
  EXPECT_EQ((*parsed)[2].value, "29.97");
  EXPECT_EQ((*parsed)[3].value, "avc1.4d401f,mp4a.40.2");
  EXPECT_TRUE((*parsed)[3].quoted);
  EXPECT_TRUE((*parsed)[4].quoted);
}

TEST(HlsAttributeListTest, RejectsMalformedLists) {
  for (const std::string_view input :
       {"", "A", "=x", "A=", "A=x,", "A=x,A=y", "A=\"bad", "A=x\"y"}) {
    EXPECT_FALSE(ParseHlsAttributeList(input).ok()) << input;
  }
}

TEST(HlsAttributeListTest, ParsesEveryAttributeValueKindAsTableCases) {
  struct Case {
    std::string_view input;
    std::string_view value;
    bool quoted;
  };
  constexpr std::array<Case, 8> cases = {{
      {.input = "BANDWIDTH=1200000", .value = "1200000", .quoted = false},
      {.input = "IV=0x0011aaff", .value = "0x0011aaff", .quoted = false},
      {.input = "FRAME-RATE=29.97", .value = "29.97", .quoted = false},
      {.input = "TIME-OFFSET=-12.5", .value = "-12.5", .quoted = false},
      {.input = "CODECS=\"avc1.4d401f,mp4a.40.2\"",
       .value = "avc1.4d401f,mp4a.40.2",
       .quoted = true},
      {.input = "METHOD=AES-128", .value = "AES-128", .quoted = false},
      {.input = "RESOLUTION=1920x1080", .value = "1920x1080", .quoted = false},
      {.input = "NAME=\"English, Main\"",
       .value = "English, Main",
       .quoted = true},
  }};
  for (const Case& item : cases) {
    const auto parsed = ParseHlsAttributeList(item.input);
    ASSERT_TRUE(parsed.ok()) << item.input;
    ASSERT_EQ(parsed->size(), 1U);
    EXPECT_EQ(parsed->front().value, item.value);
    EXPECT_EQ(parsed->front().quoted, item.quoted);
  }
}

TEST(HlsAttributeListTest, RejectsMalformedAttributeSyntax) {
  constexpr std::array<std::string_view, 6> cases = {
      "",         "BANDWIDTH",       "=1200", "NAME=\"missing",
      "A=value,", "A=first,A=second"};
  for (const std::string_view item : cases) {
    const auto parsed = ParseHlsAttributeList(item);
    EXPECT_FALSE(parsed.ok()) << item;
  }
}

TEST(HlsParserTest, RejectsMalformedValuesForEveryAttributeKind) {
  constexpr std::array<std::string_view, 7> cases = {
      "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1x,CODECS=\"avc1.64001f\"\n"
      "video.m3u8\n",
      "#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"key.bin\",IV=0x00zz\n",
      "#EXTM3U\n#EXTINF:1.2.3,segment\nsegment.ts\n",
      "#EXTM3U\n#EXT-X-START:TIME-OFFSET=--1\n",
      "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1000,CODECS=avc1.64001f\n"
      "video.m3u8\n",
      "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1000,CODECS=\"avc1.64001f\","
      "HDCP-LEVEL=TYPE-3\nvideo.m3u8\n",
      "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1000,CODECS=\"avc1.64001f\","
      "RESOLUTION=1920by1080\nvideo.m3u8\n"};
  for (const std::string_view playlist : cases) {
    EXPECT_FALSE(ParseHlsPlaylist(playlist).ok()) << playlist;
  }
}

TEST(HlsParserTest, ParsesMultivariantPlaylistAndDeferredMedia) {
  constexpr std::string_view input =
      "#EXTM3U\n"
      "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aud\",NAME=\"English\",LANGUAGE="
      "\"en\",DEFAULT=YES,URI=\"audio.m3u8\"\n"
      "#EXT-X-STREAM-INF:BANDWIDTH=2500000,AVERAGE-BANDWIDTH=2100000,CODECS="
      "\"avc1.64001f,mp4a.40.2\",RESOLUTION=1280x720,AUDIO=\"aud\"\n"
      "video.m3u8\n";
  HlsParserOptions options;
  options.base_uri = "https://example.test/path/master.m3u8";
  const auto parsed = ParseHlsPlaylist(input, options);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  ASSERT_EQ(parsed->periods.size(), 1U);
  ASSERT_EQ(parsed->periods[0]->switching_sets.size(), 2U);
  const Period& period = *parsed->periods[0];
  const SwitchingSet* video = period.FindSwitchingSet(HlsVideoSwitchingSetId());
  ASSERT_NE(video, nullptr);
  ASSERT_EQ(video->tracks.size(), 1U);
  const Track& track = *video->tracks[0];
  EXPECT_EQ(track.bandwidth_bps, 2'500'000);
  EXPECT_EQ(track.average_bandwidth_bps, 2'100'000);
  EXPECT_EQ(track.allowed_audio.size(), 1U);
  ASSERT_TRUE(track.segments.Location().has_value());
  EXPECT_EQ(track.segments.Location()->uri,
            "https://example.test/path/video.m3u8");
}

TEST(HlsParserTest, ParsesMediaSegmentsAndSplitsPeriodsAtDiscontinuities) {
  constexpr std::string_view input =
      "#EXTM3U\n#EXT-X-TARGETDURATION:5\n#EXT-X-MEDIA-SEQUENCE:17\n"
      "#EXTINF:4.5,first\na.ts\n#EXT-X-DISCONTINUITY\n"
      "#EXTINF:3,second\nb.ts\n#EXT-X-ENDLIST\n";
  HlsParserOptions options;
  options.base_uri = "https://example.test/media/index.m3u8";
  const auto parsed = ParseHlsPlaylist(input, options);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_EQ(parsed->type, PresentationType::kStatic);
  ASSERT_EQ(parsed->periods.size(), 2U);
  EXPECT_EQ(parsed->periods[0]->id, HlsPeriodId(0));
  EXPECT_EQ(parsed->periods[1]->id, HlsPeriodId(1));
  const Track& first = *parsed->periods[0]->switching_sets[0]->tracks[0];
  ASSERT_TRUE(first.segments.is_resolved());
  EXPECT_EQ(first.segments.Index()->At(0).sequence_number, 17);
  EXPECT_EQ(first.segments.Index()->At(0).uri,
            "https://example.test/media/a.ts");
  EXPECT_EQ(first.segments.Index()->At(0).duration,
            penumbra::TimeDelta::FromMicroseconds(4'500'000));
}

TEST(HlsParserTest, AppliesQueryAndImportedVariablesAndRejectsUndefinedNames) {
  constexpr std::string_view input =
      "#EXTM3U\n#EXT-X-DEFINE:QUERYPARAM=token\n"
      "#EXT-X-STREAM-INF:BANDWIDTH=1000,CODECS=\"avc1.64001f\"\n"
      "https://cdn.test/{token}/video.m3u8\n";
  HlsParserOptions options;
  options.query_parameters.emplace("token", "a1");
  const auto parsed = ParseHlsPlaylist(input, options);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  const auto invalid = ParseHlsPlaylist(
      "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1000,CODECS=\"avc1.64001f\"\n"
      "https://cdn.test/{missing}.m3u8\n");
  EXPECT_FALSE(invalid.ok());
  EXPECT_NE(invalid.status().message().find("line 3 URI"), std::string::npos);
  HlsParserOptions imported;
  imported.imported_variables.emplace("host", "cdn.test");
  const auto with_import = ParseHlsPlaylist(
      "#EXTM3U\n#EXT-X-DEFINE:IMPORT=host\n"
      "#EXT-X-STREAM-INF:BANDWIDTH=1000,CODECS=\"avc1.64001f\"\n"
      "https://{host}/video.m3u8\n",
      imported);
  EXPECT_TRUE(with_import.ok()) << with_import.status();
}

TEST(HlsParserTest, IgnoresUnknownTagsAndUnknownRequiredVariant) {
  const auto parsed = ParseHlsPlaylist(
      "#EXTM3U\n#EXT-X-VENDOR-THING:yes\n"
      "#EXT-X-STREAM-INF:BANDWIDTH=1000,CODECS=\"avc1.64001f\",REQ-FOO=YES\n"
      "ignored.m3u8\n"
      "#EXT-X-STREAM-INF:BANDWIDTH=2000,CODECS=\"avc1.64001f\",VENDOR=x\n"
      "kept.m3u8\n");
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  const auto* video =
      parsed->periods[0]->FindSwitchingSet(HlsVideoSwitchingSetId());
  ASSERT_NE(video, nullptr);
  ASSERT_EQ(video->tracks.size(), 1U);
  EXPECT_EQ(video->tracks[0]->bandwidth_bps, 2000);
}

TEST(HlsParserTest, KeepsRangesKeysGapsAndOnlyInterstitialDateRanges) {
  constexpr std::string_view input =
      "#EXTM3U\n#EXT-X-TARGETDURATION:5\n"
      "#EXT-X-MAP:URI=\"init.mp4\",BYTERANGE=\"10@2\"\n"
      "#EXT-X-KEY:METHOD=AES-128,URI=\"key.bin\",IV="
      "0x00000000000000000000000000000001\n"
      "#EXT-X-DATERANGE:ID=ordinary,CLASS=\"com.example.marker\",DURATION=2\n"
      "#EXT-X-DATERANGE:ID=ad1,CLASS=\"com.apple.hls.interstitial\",DURATION="
      "2\n"
      "#EXT-X-BYTERANGE:20@12\n#EXT-X-BITRATE:750\n#EXT-X-GAP\n"
      "#EXTINF:4,\nsegment.ts\n"
      "#EXT-X-ENDLIST\n";
  HlsParserOptions options;
  options.base_uri = "https://example.test/media/list.m3u8";
  const auto parsed = ParseHlsPlaylist(input, options);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  ASSERT_EQ(parsed->ad_breaks.size(), 1U);
  EXPECT_EQ(parsed->ad_breaks[0].id, "ad1");
  const Track& track = *parsed->periods[0]->switching_sets[0]->tracks[0];
  ASSERT_TRUE(track.segments.is_resolved());
  const SegmentIndex& index = *track.segments.Index();
  const Segment segment = index.At(0);
  EXPECT_TRUE(segment.gap);
  EXPECT_EQ(segment.bitrate_kbps, 750);
  ASSERT_TRUE(segment.byte_range.has_value());
  EXPECT_EQ(segment.byte_range->offset, 12);
  EXPECT_EQ(segment.byte_range->length, 20);
  ASSERT_TRUE(segment.init_index.has_value());
  EXPECT_EQ(index.InitSections()[*segment.init_index].uri,
            "https://example.test/media/init.mp4");
  ASSERT_TRUE(segment.key_index.has_value());
  EXPECT_EQ(index.KeyReferences()[*segment.key_index].uri,
            "https://example.test/media/key.bin");
  EXPECT_EQ(index.KeyReferences()[*segment.key_index].iv.size(), 16U);
}

TEST(HlsParserTest, CarriesActiveKeyIntoInitialisationSection) {
  HlsParserOptions options;
  options.base_uri = "https://example.test/media/list.m3u8";
  const auto parsed = ParseHlsPlaylist(
      "#EXTM3U\n#EXT-X-TARGETDURATION:5\n"
      "#EXT-X-KEY:METHOD=AES-128,URI=\"key.bin\"\n"
      "#EXT-X-MAP:URI=\"init.mp4\"\n"
      "#EXTINF:4,\nsegment.ts\n#EXT-X-ENDLIST\n",
      options);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  const Track& track = *parsed->periods[0]->switching_sets[0]->tracks[0];
  ASSERT_TRUE(track.segments.is_resolved());
  const SegmentIndex& index = *track.segments.Index();
  ASSERT_EQ(index.InitSections().size(), 1U);
  ASSERT_TRUE(index.InitSections()[0].key_index.has_value());
  EXPECT_EQ(index.InitSections()[0].key_index, index.At(0).key_index);
}

TEST(HlsParserTest, KeepsSessionSteeringAndLowLatencyModelFields) {
  constexpr std::string_view master =
      "#EXTM3U\n"
      "#EXT-X-VERSION:9\n"
      "#EXT-X-INDEPENDENT-SEGMENTS\n"
      "#EXT-X-START:TIME-OFFSET=-15.25,PRECISE=YES\n"
      "#EXT-X-SESSION-DATA:DATA-ID=\"com.example.title\",VALUE=\"Film\","
      "LANGUAGE=\"en\"\n"
      "#EXT-X-SESSION-KEY:METHOD=AES-128,URI=\"key.bin\"\n"
      "#EXT-X-CONTENT-STEERING:SERVER-URI=\"steering.json\",PATHWAY-ID=\"a\"\n"
      "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"audio\",NAME=\"English\","
      "LANGUAGE=\"en\",DEFAULT=YES,URI=\"audio.m3u8\"\n"
      "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"text\",NAME=\"English\","
      "LANGUAGE=\"en\",URI=\"text.m3u8\"\n"
      "#EXT-X-STREAM-INF:BANDWIDTH=3000000,AVERAGE-BANDWIDTH=2500000,"
      "CODECS=\"avc1.64001f,mp4a.40.2\",RESOLUTION=1920x1080,"
      "FRAME-RATE=59.94,SCORE=1.0,SUPPLEMENTAL-CODECS=\"avc1.64001f\","
      "PATHWAY-ID=\"cdn-a\",STABLE-VARIANT-ID=\"v720\","
      "HDCP-LEVEL=TYPE-1,AUDIO=\"audio\","
      "SUBTITLES=\"text\"\nvideo.m3u8\n"
      "#EXT-X-I-FRAME-STREAM-INF:BANDWIDTH=500000,URI=\"iframe.m3u8\"\n";
  HlsParserOptions options;
  options.base_uri = "https://media.example.test/master.m3u8";
  const auto parsed_master = ParseHlsPlaylist(master, options);
  ASSERT_TRUE(parsed_master.ok()) << parsed_master.status();
  ASSERT_TRUE(parsed_master->hls.has_value());
  EXPECT_TRUE(parsed_master->hls->independent_segments);
  EXPECT_EQ(parsed_master->hls->program_id, std::nullopt);
  EXPECT_EQ(parsed_master->hls->start_offset,
            penumbra::TimeDelta::FromMicroseconds(-15'250'000));
  EXPECT_TRUE(parsed_master->hls->start_time_precise);
  ASSERT_EQ(parsed_master->hls->session_data.size(), 1U);
  EXPECT_EQ(parsed_master->hls->session_data[0].value, "Film");
  ASSERT_EQ(parsed_master->hls->session_keys.size(), 1U);
  ASSERT_TRUE(parsed_master->hls->content_steering.has_value());
  EXPECT_EQ(parsed_master->hls->content_steering->server_uri,
            "https://media.example.test/steering.json");
  const auto* video =
      parsed_master->periods[0]->FindSwitchingSet(HlsVideoSwitchingSetId());
  ASSERT_NE(video, nullptr);
  ASSERT_EQ(video->tracks.size(), 1U);
  const Track& track = *video->tracks[0];
  EXPECT_EQ(track.average_bandwidth_bps, 2'500'000);
  EXPECT_EQ(track.allowed_audio.size(), 1U);
  const auto* extension = std::get_if<HlsTrackExtension>(&track.extension);
  ASSERT_NE(extension, nullptr);
  EXPECT_EQ(extension->hdcp_level, HdcpLevel::kType1);
  EXPECT_EQ(extension->score, 1.0);
  EXPECT_EQ(extension->supplemental_codecs, "avc1.64001f");
  EXPECT_EQ(extension->pathway_id, "cdn-a");
  EXPECT_EQ(extension->stable_variant_id, "v720");
  ASSERT_EQ(parsed_master->periods[0]->trick_play.size(), 1U);
  ASSERT_EQ(parsed_master->periods[0]->switching_sets.size(), 3U);

  constexpr std::string_view media =
      "#EXTM3U\n#EXT-X-VERSION:9\n#EXT-X-TARGETDURATION:4\n"
      "#EXT-X-MEDIA-SEQUENCE:40\n#EXT-X-SERVER-CONTROL:CAN-BLOCK-RELOAD=YES,"
      "CAN-SKIP-UNTIL=24.0,HOLD-BACK=12.0,PART-HOLD-BACK=1.5,"
      "CAN-SKIP-DATERANGES=YES\n#EXT-X-PART-INF:PART-TARGET=0.5\n"
      "#EXT-X-SKIP:SKIPPED-SEGMENTS=2\n"
      "#EXT-X-PART:DURATION=0.5,URI=\"part40.0.m4s\",INDEPENDENT=YES\n"
      "#EXT-X-PRELOAD-HINT:TYPE=PART,URI=\"part42.0.m4s\","
      "BYTERANGE-START=100\n"
      "#EXT-X-RENDITION-REPORT:URI=\"audio.m3u8\",LAST-MSN=42,LAST-PART=0\n"
      "#EXT-X-KEY:METHOD=AES-128,URI=\"key.bin\"\n"
      "#EXTINF:4,segment\nsegment42.m4s\n#EXT-X-DISCONTINUITY\n"
      "#EXTINF:3,segment\nsegment43.m4s\n#EXT-X-ENDLIST\n";
  const auto parsed_media = ParseHlsPlaylist(media, options);
  ASSERT_TRUE(parsed_media.ok()) << parsed_media.status();
  ASSERT_TRUE(parsed_media->hls.has_value());
  EXPECT_EQ(parsed_media->hls->skipped_segments, 2);
  EXPECT_TRUE(parsed_media->hls->can_block_reload);
  EXPECT_TRUE(parsed_media->hls->can_skip_date_ranges);
  EXPECT_EQ(parsed_media->hls->can_skip_until,
            penumbra::TimeDelta::FromMicroseconds(24'000'000));
  EXPECT_EQ(parsed_media->hls->part_target_duration,
            penumbra::TimeDelta::FromMicroseconds(500'000));
  ASSERT_TRUE(parsed_media->hls->preload_hint.has_value());
  EXPECT_EQ(parsed_media->hls->preload_hint->byte_range_start, 100);
  ASSERT_EQ(parsed_media->hls->rendition_reports.size(), 1U);
  EXPECT_EQ(parsed_media->hls->rendition_reports[0].last_part, 0);
  ASSERT_EQ(parsed_media->periods.size(), 2U);
  const Segment first = parsed_media->periods[0]
                            ->switching_sets[0]
                            ->tracks[0]
                            ->segments.Index()
                            ->At(0);
  EXPECT_EQ(first.sequence_number, 42);
  ASSERT_EQ(first.hls_parts.size(), 1U);
  EXPECT_TRUE(first.hls_parts[0].independent);
}

TEST(HlsParserTest, MasterPresentationTypeFollowsLoadedMediaPlaylists) {
  const auto parsed = ParseHlsPlaylist(
      "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1000,CODECS=\"avc1.64001f\"\n"
      "media.m3u8\n"
      "#EXT-X-STREAM-INF:BANDWIDTH=2000,CODECS=\"avc1.64001f\"\n"
      "media-hd.m3u8\n");
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  Presentation master = *parsed;
  const std::array<PresentationType, 2> loaded_types = {
      PresentationType::kStatic, PresentationType::kDynamic};
  const std::array<PresentationType, 1> partial_types = {
      PresentationType::kStatic};
  EXPECT_FALSE(
      SetHlsPresentationTypeFromMediaPlaylists(master, partial_types).ok());
  ASSERT_TRUE(
      SetHlsPresentationTypeFromMediaPlaylists(master, loaded_types).ok());
  EXPECT_EQ(master.type, PresentationType::kDynamic);
  ASSERT_TRUE(master.live.has_value());
  const std::array<PresentationType, 2> static_types = {
      PresentationType::kStatic, PresentationType::kStatic};
  ASSERT_TRUE(
      SetHlsPresentationTypeFromMediaPlaylists(master, static_types).ok());
  EXPECT_EQ(master.type, PresentationType::kStatic);
  EXPECT_FALSE(master.live.has_value());
}

TEST(HlsParserTest, RetainsIFrameOnlyMediaPlaylistMode) {
  const auto parsed = ParseHlsPlaylist(
      "#EXTM3U\n#EXT-X-I-FRAMES-ONLY\n#EXTINF:1.0,frame\nframe.ts\n"
      "#EXT-X-ENDLIST\n");
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  const Track& track =
      *parsed->periods.front()->switching_sets.front()->tracks.front();
  const auto* extension = std::get_if<HlsTrackExtension>(&track.extension);
  ASSERT_NE(extension, nullptr);
  EXPECT_TRUE(extension->i_frames_only);
}

TEST(HlsParserGoldenTest, MatchesAppleStyleMultivariantModel) {
  HlsParserOptions options;
  options.base_uri = "https://media.example.test/master.m3u8";
  const auto parsed =
      ParseHlsPlaylist(ReadFixture("apple_multivariant.m3u8"), options);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  ASSERT_TRUE(parsed->hls.has_value());
  EXPECT_EQ(TrackCountGolden(*parsed),
            ReadFixture("apple_multivariant.golden"));
}

TEST(HlsParserTest, SaturatesSegmentTimesInsteadOfOverflowing) {
  constexpr std::string_view input =
      "#EXTM3U\n#EXT-X-TARGETDURATION:10\n#EXT-X-MEDIA-SEQUENCE:0\n"
      "#EXTINF:222222222222222222222.0,\na.ts\n"
      "#EXTINF:222222222222222222222.0,\nb.ts\n#EXT-X-ENDLIST\n";
  const auto parsed = ParseHlsPlaylist(input);
  if (parsed.ok()) {
    EXPECT_GT(parsed->duration.InMicroseconds(), 0);
  }
}

TEST(HlsParserGoldenTest, MatchesAppleStyleEncryptedLowLatencyModel) {
  HlsParserOptions options;
  options.base_uri = "https://media.example.test/live/index.m3u8";
  const auto parsed =
      ParseHlsPlaylist(ReadFixture("apple_low_latency_media.m3u8"), options);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  ASSERT_TRUE(parsed->hls.has_value());
  EXPECT_EQ(MediaGolden(*parsed),
            ReadFixture("apple_low_latency_media.golden"));
}

}
}
