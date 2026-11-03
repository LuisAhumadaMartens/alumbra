#include "umbra/manifest/presentation.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "penumbra/interface/encryption_info.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/stream_info.h"
#include "test_util.h"
#include "umbra/codec/codec_string.h"
#include "umbra/manifest/ids.h"
#include "umbra/manifest/refresh.h"
#include "umbra/manifest/segment_index.h"
#include "umbra/manifest/validate.h"

namespace umbra::manifest {
namespace {

using penumbra::MediaTime;
using penumbra::StreamType;
using penumbra::TimeDelta;
using test_data::Delta;
using test_data::MakeAudioTrack;
using test_data::MakeExplicitIndex;
using test_data::MakeSet;
using test_data::MakeVideoTrack;
using test_data::MakeVodPresentation;
using test_data::Seconds;

Period MutablePeriod(const Presentation& presentation, std::size_t index) {
  return *presentation.periods[index];
}

SwitchingSet MutableSet(const Period& period, std::size_t index) {
  return *period.switching_sets[index];
}

template <typename Edit>
absl::Status ValidateAfter(Edit edit) {
  Presentation presentation = MakeVodPresentation();
  edit(presentation);
  return ValidatePresentation(presentation);
}

template <typename Edit>
absl::Status ValidateEditedSet(Edit edit) {
  return ValidateAfter([&](Presentation& presentation) {
    Period period = MutablePeriod(presentation, 0);
    SwitchingSet set = MutableSet(period, 0);
    edit(set);
    period.switching_sets[0] = Freeze(std::move(set));
    presentation.periods[0] = Freeze(std::move(period));
  });
}

template <typename Edit>
absl::Status ValidateEditedTrack(Edit edit) {
  return ValidateEditedSet([&](SwitchingSet& set) {
    Track track = *set.tracks[0];
    edit(track);
    set.tracks[0] = Freeze(std::move(track));
  });
}

TEST(ValidatePresentationTest, AcceptsAWellFormedVodPresentation) {
  EXPECT_TRUE(ValidatePresentation(MakeVodPresentation()).ok());
  const auto built = BuildPresentation(MakeVodPresentation());
  ASSERT_TRUE(built.ok()) << built.status();
  EXPECT_EQ((*built)->periods.size(), 1U);
}

TEST(ValidatePresentationTest, RejectsTheWrongLiveInformation) {
  EXPECT_FALSE(ValidateAfter([](Presentation& p) {
                 p.type = PresentationType::kDynamic;
               }).ok());
  EXPECT_FALSE(
      ValidateAfter([](Presentation& p) { p.live = LiveInfo(); }).ok());
  EXPECT_FALSE(ValidateAfter([](Presentation& p) {
                 p.duration = TimeDelta::Unknown();
               }).ok());
}

TEST(ValidatePresentationTest,
     ADynamicPresentationMayHaveAnOpenEndedLastPeriod) {
  Presentation presentation = MakeVodPresentation();
  presentation.type = PresentationType::kDynamic;
  presentation.duration = TimeDelta::Unknown();
  LiveInfo live;
  live.window_start = Seconds(0);
  live.window_end = Seconds(20);
  live.time_shift_depth = Delta(30);
  presentation.live = live;
  Period period = MutablePeriod(presentation, 0);
  period.duration = TimeDelta::Unknown();
  presentation.periods[0] = Freeze(std::move(period));
  EXPECT_TRUE(ValidatePresentation(presentation).ok());
  EXPECT_EQ(presentation.FindPeriod(Seconds(1'000'000))->id, PeriodId("p0"));

  presentation.live->hold_back = Delta(-1);
  EXPECT_FALSE(ValidatePresentation(presentation).ok());
  presentation.live->hold_back = std::nullopt;
  presentation.live->window_end = Seconds(-5);
  EXPECT_FALSE(ValidatePresentation(presentation).ok());
}

TEST(ValidatePresentationTest, ChecksPeriodOrderAndDurations) {
  EXPECT_FALSE(ValidateAfter([](Presentation& p) { p.periods.clear(); }).ok());
  EXPECT_FALSE(ValidateAfter([](Presentation& p) {
                 Period period = MutablePeriod(p, 0);
                 period.duration = TimeDelta::Unknown();
                 p.periods[0] = Freeze(std::move(period));
               }).ok());
  EXPECT_FALSE(ValidateAfter([](Presentation& p) {
                 Period period = MutablePeriod(p, 0);
                 period.duration = Delta(0);
                 p.periods[0] = Freeze(std::move(period));
               }).ok());
  EXPECT_FALSE(ValidateAfter([](Presentation& p) {
                 Period period = MutablePeriod(p, 0);
                 period.start = Seconds(-1);
                 p.periods[0] = Freeze(std::move(period));
               }).ok());
  EXPECT_FALSE(ValidateAfter([](Presentation& p) {
                 Period second = MutablePeriod(p, 0);
                 second.start = Seconds(10);
                 p.periods.push_back(Freeze(std::move(second)));
               }).ok());
  EXPECT_FALSE(ValidateAfter([](Presentation& p) {
                 Period second = MutablePeriod(p, 0);
                 second.start = Seconds(20);
                 p.periods.push_back(Freeze(std::move(second)));
               }).ok());
  EXPECT_TRUE(ValidateAfter([](Presentation& p) {
                Period second = MutablePeriod(p, 0);
                second.id = PeriodId("p1");
                second.start = Seconds(20);
                p.periods.push_back(Freeze(std::move(second)));
                p.duration = Delta(40);
              }).ok());
  EXPECT_FALSE(
      ValidateAfter([](Presentation& p) { p.periods[0] = nullptr; }).ok());
}

TEST(ValidatePresentationTest, ChecksSwitchingSets) {
  EXPECT_FALSE(
      ValidateEditedSet([](SwitchingSet& s) { s.id = SwitchingSetId(); }).ok());
  EXPECT_FALSE(
      ValidateEditedSet([](SwitchingSet& s) { s.tracks.clear(); }).ok());
  EXPECT_FALSE(ValidateEditedSet([](SwitchingSet& s) {
                 s.type = StreamType::kAudio;
               }).ok());
  EXPECT_FALSE(ValidateEditedSet([](SwitchingSet& s) {
                 s.label = std::string(100'000, 'x');
               }).ok());
  EXPECT_FALSE(ValidateEditedSet([](SwitchingSet& s) {
                 s.channels.channel_count = -1;
               }).ok());
  EXPECT_FALSE(
      ValidateEditedSet([](SwitchingSet& s) { s.tracks[0] = nullptr; }).ok());
  EXPECT_FALSE(ValidateAfter([](Presentation& p) {
                 Period period = MutablePeriod(p, 0);
                 period.switching_sets[1] = period.switching_sets[0];
                 p.periods[0] = Freeze(std::move(period));
               }).ok());
}

TEST(ValidatePresentationTest, ChecksTracks) {
  EXPECT_TRUE(ValidateEditedTrack([](Track&) {}).ok());
  EXPECT_FALSE(ValidateEditedTrack([](Track& t) { t.id = TrackId(); }).ok());
  EXPECT_FALSE(ValidateEditedTrack([](Track& t) {
                 t.id = TrackId(std::string(kMaxIdBytes + 1, 'a'));
               }).ok());
  EXPECT_FALSE(ValidateEditedTrack([](Track& t) { t.bandwidth_bps = 0; }).ok());
  EXPECT_FALSE(
      ValidateEditedTrack([](Track& t) { t.bandwidth_bps = -5; }).ok());
  EXPECT_FALSE(
      ValidateEditedTrack([](Track& t) { t.average_bandwidth_bps = -1; }).ok());
  EXPECT_FALSE(ValidateEditedTrack([](Track& t) {
                 std::get<VideoTrackFormat>(t.format).width = 0;
               }).ok());
  EXPECT_FALSE(ValidateEditedTrack([](Track& t) {
                 std::get<VideoTrackFormat>(t.format).bit_depth = 7;
               }).ok());
  EXPECT_FALSE(ValidateEditedTrack([](Track& t) {
                 std::get<VideoTrackFormat>(t.format).frame_rate =
                     penumbra::Rational::Of(0, 1);
               }).ok());
  EXPECT_FALSE(ValidateEditedTrack([](Track& t) {
                 std::get<VideoTrackFormat>(t.format).frame_rate =
                     penumbra::Rational::Of(5000, 1);
               }).ok());
  EXPECT_TRUE(ValidateEditedTrack([](Track& t) {
                std::get<VideoTrackFormat>(t.format).frame_rate =
                    penumbra::Rational::Of(120, 1);
                std::get<VideoTrackFormat>(t.format).bit_depth = 10;
                std::get<VideoTrackFormat>(t.format).range = VideoRange::kPq;
              }).ok());
  EXPECT_FALSE(ValidateEditedTrack([](Track& t) {
                 t.timeline.media_origin = MediaTime::Unknown();
               }).ok());
  EXPECT_FALSE(ValidateEditedTrack([](Track& t) {
                 t.segments = SegmentIndexProvider();
               }).ok());
  EXPECT_TRUE(ValidateEditedTrack([](Track& t) {
                IndexLocation location;
                location.uri = "https://x.test/media.m3u8";
                t.segments = SegmentIndexProvider::Deferred(location);
              }).ok());
}

TEST(ValidateTrackTest, AllowsUnknownHlsBandwidthForDeferredMedia) {
  Track track = MakeVideoTrack("hls-unknown-bandwidth", 100'000, 720,
                               MakeExplicitIndex(0, 1));
  track.bandwidth_bps.reset();
  track.extension = HlsTrackExtension{};
  EXPECT_TRUE(ValidateTrack(track).ok());
  track.extension = std::monostate{};
  EXPECT_FALSE(ValidateTrack(track).ok());
}

TEST(ValidatePresentationTest, ChecksCodecsAgainstTheTrackType) {
  const auto avc = codec::ParseCodecString("avc1.64001f");
  const auto aac = codec::ParseCodecString("mp4a.40.2");
  ASSERT_TRUE(avc.ok());
  ASSERT_TRUE(aac.ok());
  EXPECT_TRUE(
      ValidateEditedTrack([&](Track& t) { t.codecs = {*avc, *aac}; }).ok());
  EXPECT_FALSE(ValidateEditedTrack([&](Track& t) {
                 t.codecs.assign(kMaxCodecsPerTrack + 1, *avc);
               }).ok());
  const absl::Status audio_with_video_codec =
      ValidateAfter([&](Presentation& p) {
        Period period = MutablePeriod(p, 0);
        SwitchingSet set = MutableSet(period, 1);
        Track track = *set.tracks[0];
        track.codecs = {*avc};
        set.tracks[0] = Freeze(std::move(track));
        period.switching_sets[1] = Freeze(std::move(set));
        p.periods[0] = Freeze(std::move(period));
      });
  EXPECT_FALSE(audio_with_video_codec.ok());
}

TEST(ValidatePresentationTest, AudioCouplingMustNameAudioSetsOfTheSamePeriod) {
  const auto couple = [](Presentation& p, const std::string& set_id) {
    Period period = MutablePeriod(p, 0);
    SwitchingSet set = MutableSet(period, 0);
    Track track = *set.tracks[0];
    track.allowed_audio = {SwitchingSetId(set_id)};
    set.tracks[0] = Freeze(std::move(track));
    period.switching_sets[0] = Freeze(std::move(set));
    p.periods[0] = Freeze(std::move(period));
  };
  EXPECT_TRUE(ValidateAfter([&](Presentation& p) { couple(p, "audio"); }).ok());
  EXPECT_FALSE(
      ValidateAfter([&](Presentation& p) { couple(p, "missing"); }).ok());
  EXPECT_FALSE(
      ValidateAfter([&](Presentation& p) { couple(p, "video"); }).ok());
  EXPECT_FALSE(ValidateEditedTrack([](Track& t) {
                 t.allowed_audio = {SwitchingSetId("audio"),
                                    SwitchingSetId("audio")};
               }).ok());
  const absl::Status on_audio = ValidateAfter([](Presentation& p) {
    Period period = MutablePeriod(p, 0);
    SwitchingSet set = MutableSet(period, 1);
    Track track = *set.tracks[0];
    track.allowed_audio = {SwitchingSetId("audio")};
    set.tracks[0] = Freeze(std::move(track));
    period.switching_sets[1] = Freeze(std::move(set));
    p.periods[0] = Freeze(std::move(period));
  });
  EXPECT_FALSE(on_audio.ok());
}

TEST(ValidatePresentationTest, TrackIdsAreUniqueAcrossSetsAndTrickPlay) {
  EXPECT_FALSE(ValidateAfter([](Presentation& p) {
                 Period period = MutablePeriod(p, 0);
                 Track clash = *period.switching_sets[0]->tracks[0];
                 period.trick_play.push_back(Freeze(std::move(clash)));
                 p.periods[0] = Freeze(std::move(period));
               }).ok());
  EXPECT_TRUE(ValidateAfter([](Presentation& p) {
                Period period = MutablePeriod(p, 0);
                Track trick = *period.switching_sets[0]->tracks[0];
                trick.id = TrackId("v1-iframe");
                period.trick_play.push_back(Freeze(std::move(trick)));
                p.periods[0] = Freeze(std::move(period));
              }).ok());
  EXPECT_FALSE(ValidateAfter([](Presentation& p) {
                 Period period = MutablePeriod(p, 0);
                 Track trick = *period.switching_sets[1]->tracks[0];
                 trick.id = TrackId("a1-trick");
                 period.trick_play.push_back(Freeze(std::move(trick)));
                 p.periods[0] = Freeze(std::move(period));
               }).ok());
}

TEST(ValidatePresentationTest, ChecksProtection) {
  ProtectionInfo protection;
  protection.scheme = penumbra::EncryptionScheme::kCbcs;
  protection.system_ids = {SystemId{1}, SystemId{2}};
  InitData init;
  init.system_id = SystemId{1};
  init.data = {1, 2, 3};
  protection.init_data = {init};
  protection.key_uri = "skd://key";
  EXPECT_TRUE(ValidateEditedSet([&](SwitchingSet& s) {
                s.protection = protection;
              }).ok());
  EXPECT_FALSE(ValidateEditedSet([&](SwitchingSet& s) {
                 ProtectionInfo copy = protection;
                 copy.system_ids = {SystemId{1}, SystemId{1}};
                 s.protection = copy;
               }).ok());
  EXPECT_FALSE(ValidateEditedSet([&](SwitchingSet& s) {
                 ProtectionInfo copy = protection;
                 copy.init_data[0].data.assign(kMaxInitDataBytes + 1, 0);
                 s.protection = copy;
               }).ok());
  EXPECT_FALSE(ValidateEditedSet([&](SwitchingSet& s) {
                 ProtectionInfo copy = protection;
                 copy.key_uri = "";
                 s.protection = copy;
               }).ok());
}

TEST(ValidatePresentationTest, ExtensionsMustMatchTheProtocol) {
  EXPECT_TRUE(ValidateEditedTrack([](Track& t) {
                t.extension = DashTrackExtension();
              }).ok());
  EXPECT_FALSE(ValidateEditedTrack([](Track& t) {
                 t.extension = HlsTrackExtension();
               }).ok());
  Presentation hls = MakeVodPresentation();
  hls.protocol = Protocol::kHls;
  Period period = MutablePeriod(hls, 0);
  SwitchingSet set = MutableSet(period, 0);
  Track track = *set.tracks[0];
  track.extension = HlsTrackExtension();
  set.tracks[0] = Freeze(std::move(track));
  period.switching_sets[0] = Freeze(std::move(set));
  hls.periods[0] = Freeze(std::move(period));
  EXPECT_TRUE(ValidatePresentation(hls).ok());
}

TEST(ValidatePresentationTest, ChecksAdBreaks) {
  AdBreak ad_break;
  ad_break.id = "break-1";
  ad_break.start = Seconds(8);
  ad_break.duration = Delta(15);
  EXPECT_TRUE(
      ValidateAfter([&](Presentation& p) { p.ad_breaks = {ad_break}; }).ok());
  EXPECT_FALSE(ValidateAfter([&](Presentation& p) {
                 p.ad_breaks = {ad_break, ad_break};
               }).ok());
  EXPECT_FALSE(ValidateAfter([&](Presentation& p) {
                 AdBreak bad = ad_break;
                 bad.id.clear();
                 p.ad_breaks = {bad};
               }).ok());
  EXPECT_FALSE(ValidateAfter([&](Presentation& p) {
                 AdBreak bad = ad_break;
                 bad.start = MediaTime::Unknown();
                 p.ad_breaks = {bad};
               }).ok());
  EXPECT_FALSE(ValidateAfter([&](Presentation& p) {
                 AdBreak bad = ad_break;
                 bad.duration = Delta(-1);
                 p.ad_breaks = {bad};
               }).ok());
  EXPECT_TRUE(ValidateAfter([&](Presentation& p) {
                AdBreak open = ad_break;
                open.duration = TimeDelta::Unknown();
                p.ad_breaks = {open};
              }).ok());
}

TEST(PresentationTest, PeriodLookupIsExactAtEveryBoundary) {
  Presentation presentation = MakeVodPresentation();
  Period second = MutablePeriod(presentation, 0);
  second.id = PeriodId("p1");
  second.start = Seconds(30);
  second.duration = Delta(10);
  presentation.periods.push_back(Freeze(std::move(second)));
  presentation.duration = Delta(40);
  ASSERT_TRUE(ValidatePresentation(presentation).ok());
  const TimeDelta tick = TimeDelta::FromMicroseconds(1);
  EXPECT_EQ(presentation.FindPeriod(Seconds(0))->id, PeriodId("p0"));
  EXPECT_EQ(presentation.FindPeriod(Seconds(0) - tick), nullptr);
  EXPECT_EQ(presentation.FindPeriod(Seconds(20) - tick)->id, PeriodId("p0"));
  EXPECT_EQ(presentation.FindPeriod(Seconds(20)), nullptr);
  EXPECT_EQ(presentation.FindPeriod(Seconds(30) - tick), nullptr);
  EXPECT_EQ(presentation.FindPeriod(Seconds(30))->id, PeriodId("p1"));
  EXPECT_EQ(presentation.FindPeriod(Seconds(40) - tick)->id, PeriodId("p1"));
  EXPECT_EQ(presentation.FindPeriod(Seconds(40)), nullptr);
  EXPECT_EQ(presentation.FindPeriod(MediaTime::Unknown()), nullptr);
  EXPECT_EQ(presentation.FindPeriodById(PeriodId("p1"))->start, Seconds(30));
  EXPECT_EQ(presentation.FindPeriodById(PeriodId("nope")), nullptr);
}

TEST(PresentationTest, NavigationFindsSetsAndTracks) {
  const Presentation presentation = MakeVodPresentation();
  const Period& period = *presentation.periods[0];
  EXPECT_EQ(period.FindSwitchingSet(SwitchingSetId("audio"))->type,
            StreamType::kAudio);
  EXPECT_EQ(period.FindTrack(TrackId("v2"))->bandwidth_bps, 5'000'000);
  EXPECT_EQ(period.FindSwitchingSetOf(TrackId("a1"))->id,
            SwitchingSetId("audio"));
  EXPECT_EQ(period.FindTrack(TrackId("zzz")), nullptr);
  EXPECT_EQ(period.switching_sets[0]->tracks[0]->type(), StreamType::kVideo);
}

TEST(PresentationTest, TimelineMappingRoundTrips) {
  TimelineMapping mapping;
  mapping.media_origin = Seconds(10);
  mapping.presentation_origin = Seconds(100);
  EXPECT_EQ(mapping.ToPresentation(Seconds(15)), Seconds(105));
  EXPECT_EQ(mapping.ToMedia(Seconds(105)), Seconds(15));
  EXPECT_EQ(mapping.ToMedia(mapping.ToPresentation(Seconds(3))), Seconds(3));
}

TEST(PresentationTest, SwitchingSetCodecsAreDeduplicated) {
  const auto avc = codec::ParseCodecString("avc1.64001f");
  ASSERT_TRUE(avc.ok());
  const Presentation presentation = MakeVodPresentation();
  const Period period = MutablePeriod(presentation, 0);
  SwitchingSet set = MutableSet(period, 0);
  for (std::shared_ptr<const Track>& track : set.tracks) {
    Track copy = *track;
    copy.codecs = {*avc};
    track = Freeze(std::move(copy));
  }
  EXPECT_EQ(set.CodecIds(),
            std::vector<penumbra::CodecId>{penumbra::CodecId::kH264});
}

TEST(RefreshPresentationTest, AFirstSnapshotIsJustBuilt) {
  const auto snapshot = RefreshPresentation(nullptr, MakeVodPresentation());
  ASSERT_TRUE(snapshot.ok());
  EXPECT_EQ((*snapshot)->generation, 0U);
}

TEST(RefreshPresentationTest, AnIdenticalRefreshReturnsTheSameSnapshot) {
  const auto first = BuildPresentation(MakeVodPresentation());
  ASSERT_TRUE(first.ok());
  const auto again = RefreshPresentation(*first, MakeVodPresentation());
  ASSERT_TRUE(again.ok()) << again.status();
  EXPECT_EQ(again->get(), first->get());
}

Presentation LivePresentation(std::int64_t first_sequence, std::size_t count,
                              std::int64_t audio_first = 0,
                              std::size_t audio_count = 3) {
  Presentation presentation;
  presentation.protocol = Protocol::kHls;
  presentation.type = PresentationType::kDynamic;
  LiveInfo live;
  live.window_start = Seconds(first_sequence * 4);
  live.window_end =
      Seconds((first_sequence + static_cast<std::int64_t>(count)) * 4);
  presentation.live = live;
  std::vector<Track> video;
  video.push_back(MakeVideoTrack("v1", 2'000'000, 720,
                                 MakeExplicitIndex(first_sequence, count)));
  std::vector<Track> audio;
  audio.push_back(MakeAudioTrack("a1", 128'000,
                                 MakeExplicitIndex(audio_first, audio_count)));
  Period period;
  period.id = PeriodId("hls/d/0");
  period.start = Seconds(0);
  period.duration = TimeDelta::Unknown();
  period.switching_sets = {
      MakeSet("video", StreamType::kVideo, std::move(video)),
      MakeSet("audio", StreamType::kAudio, std::move(audio))};
  presentation.periods.push_back(Freeze(std::move(period)));
  return presentation;
}

TEST(RefreshPresentationTest,
     ASlidingWindowSharesUnchangedNodesAndKeepsIdentity) {
  const auto first = BuildPresentation(LivePresentation(10, 6));
  ASSERT_TRUE(first.ok()) << first.status();
  const auto second = RefreshPresentation(*first, LivePresentation(12, 6));
  ASSERT_TRUE(second.ok()) << second.status();
  ASSERT_NE(second->get(), first->get());
  EXPECT_EQ((*second)->generation, 1U);

  const Period& before = *(*first)->periods[0];
  const Period& after = *(*second)->periods[0];
  const Track* video_before = before.FindTrack(TrackId("v1"));
  const Track* video_after = after.FindTrack(TrackId("v1"));
  EXPECT_NE(video_before, video_after);
  EXPECT_EQ(video_after->segments.Index()->FirstSequenceNumber(), 12);
  EXPECT_EQ(video_after->segments.Index()->LastSequenceNumber(), 17);

  EXPECT_EQ(before.FindTrack(TrackId("a1")), after.FindTrack(TrackId("a1")));
  EXPECT_EQ(before.FindSwitchingSet(SwitchingSetId("audio")),
            after.FindSwitchingSet(SwitchingSetId("audio")));
  EXPECT_NE(before.FindSwitchingSet(SwitchingSetId("video")),
            after.FindSwitchingSet(SwitchingSetId("video")));
}

TEST(RefreshPresentationTest, TrackIdsSurviveARefresh) {
  const auto first = BuildPresentation(LivePresentation(0, 4));
  ASSERT_TRUE(first.ok());
  auto current = *first;
  for (std::int64_t step = 1; step <= 20; ++step) {
    const auto next = RefreshPresentation(current, LivePresentation(step, 4));
    ASSERT_TRUE(next.ok()) << next.status();
    EXPECT_NE((*next)->periods[0]->FindTrack(TrackId("v1")), nullptr);
    EXPECT_EQ((*next)->generation, static_cast<std::uint64_t>(step));
    current = *next;
  }
}

TEST(RefreshPresentationTest, ARenumberedWindowIsRejected) {
  const auto first = BuildPresentation(LivePresentation(10, 6));
  ASSERT_TRUE(first.ok());
  Presentation bad = LivePresentation(12, 6);
  Period period = MutablePeriod(bad, 0);
  SwitchingSet set = MutableSet(period, 0);
  Track track = *set.tracks[0];
  ExplicitSegmentIndexParameters shifted;
  for (std::int64_t i = 0; i < 6; ++i) {
    shifted.segments.push_back(
        test_data::MakeSegment(12 + i, ((12 + i) * 4) + 1, 4));
  }
  const auto renumbered = ExplicitSegmentIndex::Create(std::move(shifted));
  ASSERT_TRUE(renumbered.ok());
  track.segments = SegmentIndexProvider::Resolved(*renumbered);
  set.tracks[0] = Freeze(std::move(track));
  period.switching_sets[0] = Freeze(std::move(set));
  bad.periods[0] = Freeze(std::move(period));
  EXPECT_FALSE(RefreshPresentation(*first, std::move(bad)).ok());
}

TEST(RefreshPresentationTest, RejectsProtocolChangesAndResurrectedLiveStreams) {
  const auto live = BuildPresentation(LivePresentation(0, 4));
  ASSERT_TRUE(live.ok());
  Presentation other_protocol = LivePresentation(0, 4);
  other_protocol.protocol = Protocol::kDash;
  EXPECT_FALSE(RefreshPresentation(*live, std::move(other_protocol)).ok());

  const auto finished = BuildPresentation(MakeVodPresentation());
  ASSERT_TRUE(finished.ok());
  Presentation relive = LivePresentation(0, 4);
  relive.protocol = Protocol::kDash;
  EXPECT_FALSE(RefreshPresentation(*finished, std::move(relive)).ok());
}

TEST(RefreshPresentationTest, ALiveStreamMayEnd) {
  const auto live = BuildPresentation(LivePresentation(0, 4));
  ASSERT_TRUE(live.ok());
  Presentation ended = MakeVodPresentation(4);
  ended.protocol = Protocol::kHls;
  const auto result = RefreshPresentation(*live, std::move(ended));
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ((*result)->type, PresentationType::kStatic);
}

TEST(RefreshPresentationTest, ADeferredTrackKeepsThePreviouslyLoadedIndex) {
  IndexLocation location;
  location.uri = "https://x.test/v1.m3u8";

  Presentation loaded = LivePresentation(0, 4);
  {
    Period period = MutablePeriod(loaded, 0);
    SwitchingSet set = MutableSet(period, 0);
    Track track = *set.tracks[0];
    track.segments = SegmentIndexProvider::Deferred(location).WithIndex(
        MakeExplicitIndex(0, 4));
    set.tracks[0] = Freeze(std::move(track));
    period.switching_sets[0] = Freeze(std::move(set));
    loaded.periods[0] = Freeze(std::move(period));
  }
  const auto base = BuildPresentation(std::move(loaded));
  ASSERT_TRUE(base.ok());

  Presentation master_only = LivePresentation(0, 4);
  {
    Period period = MutablePeriod(master_only, 0);
    SwitchingSet set = MutableSet(period, 0);
    Track track = *set.tracks[0];
    track.segments = SegmentIndexProvider::Deferred(location);
    set.tracks[0] = Freeze(std::move(track));
    period.switching_sets[0] = Freeze(std::move(set));
    master_only.periods[0] = Freeze(std::move(period));
  }
  const auto refreshed = RefreshPresentation(*base, std::move(master_only));
  ASSERT_TRUE(refreshed.ok()) << refreshed.status();
  EXPECT_EQ(refreshed->get(), base->get());
}

Presentation EquivalentFixture(Protocol protocol) {
  Presentation presentation;
  presentation.protocol = protocol;
  presentation.type = PresentationType::kStatic;
  presentation.duration = Delta(20);
  const bool dash = protocol == Protocol::kDash;
  std::vector<Track> video;
  video.push_back(MakeVideoTrack(dash ? "dash/1/hi" : "hls/v/5000000",
                                 5'000'000, 1080, MakeExplicitIndex(0, 5)));
  video.push_back(MakeVideoTrack(dash ? "dash/1/lo" : "hls/v/2000000",
                                 2'000'000, 720, MakeExplicitIndex(0, 5)));
  std::vector<Track> audio;
  audio.push_back(MakeAudioTrack(dash ? "dash/2/en" : "hls/audio/en", 128'000,
                                 MakeExplicitIndex(0, 5)));
  Period period;
  period.id = PeriodId(dash ? "dash/p/0" : "hls/d/0");
  period.start = Seconds(0);
  period.duration = Delta(20);
  period.switching_sets = {MakeSet(dash ? "dash/1" : "hls/video",
                                   StreamType::kVideo, std::move(video)),
                           MakeSet(dash ? "dash/2" : "hls/audio/en",
                                   StreamType::kAudio, std::move(audio))};
  presentation.periods.push_back(Freeze(std::move(period)));
  return presentation;
}

TEST(EquivalentTest, HlsAndDashModelsOfTheSameContentAreEquivalent) {
  const Presentation hls = EquivalentFixture(Protocol::kHls);
  const Presentation dash = EquivalentFixture(Protocol::kDash);
  EXPECT_TRUE(ValidatePresentation(hls).ok());
  EXPECT_TRUE(ValidatePresentation(dash).ok());
  EXPECT_FALSE(SameContent(hls, dash));
  EXPECT_TRUE(Equivalent(hls, dash));
  EXPECT_TRUE(Equivalent(dash, hls));
}

TEST(EquivalentTest, ADifferenceInContentBreaksTheEquivalence) {
  const Presentation hls = EquivalentFixture(Protocol::kHls);
  Presentation changed = EquivalentFixture(Protocol::kDash);
  Period period = MutablePeriod(changed, 0);
  SwitchingSet set = MutableSet(period, 0);
  Track track = *set.tracks[0];
  track.segments = SegmentIndexProvider::Resolved(MakeExplicitIndex(0, 4));
  set.tracks[0] = Freeze(std::move(track));
  period.switching_sets[0] = Freeze(std::move(set));
  changed.periods[0] = Freeze(std::move(period));
  EXPECT_FALSE(Equivalent(hls, changed));

  Presentation louder = EquivalentFixture(Protocol::kDash);
  Period audio_period = MutablePeriod(louder, 0);
  SwitchingSet audio_set = MutableSet(audio_period, 1);
  Track audio_track = *audio_set.tracks[0];
  audio_track.bandwidth_bps = 256'000;
  audio_set.tracks[0] = Freeze(std::move(audio_track));
  audio_period.switching_sets[1] = Freeze(std::move(audio_set));
  louder.periods[0] = Freeze(std::move(audio_period));
  EXPECT_FALSE(Equivalent(hls, louder));
}

}
}
