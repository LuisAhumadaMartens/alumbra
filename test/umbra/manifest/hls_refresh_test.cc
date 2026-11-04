#include "umbra/manifest/hls_refresh.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/stream_info.h"
#include "test_util.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/error_detail.h"
#include "umbra/base/status.h"
#include "umbra/manifest/ids.h"
#include "umbra/manifest/presentation.h"
#include "umbra/manifest/refresh.h"
#include "umbra/manifest/segment.h"
#include "umbra/manifest/segment_index.h"
#include "umbra/manifest/validate.h"

namespace umbra::manifest {
namespace {

using penumbra::MediaTime;
using penumbra::TimeDelta;
using test_data::Delta;
using test_data::MakeAudioTrack;
using test_data::MakeExplicitIndex;
using test_data::MakeSegment;
using test_data::MakeSet;
using test_data::MakeVideoTrack;
using test_data::Seconds;

Presentation LivePresentation(std::int64_t first_sequence, bool with_index) {
  Presentation presentation;
  presentation.protocol = Protocol::kHls;
  presentation.type = PresentationType::kDynamic;
  LiveInfo live;
  live.window_start = Seconds(0);
  live.window_end = Seconds((first_sequence + 3) * 4);
  live.target_segment_duration = Delta(6);
  presentation.live = live;
  presentation.hls = HlsPresentationExtension();
  std::vector<Track> video;
  Track video_track =
      MakeVideoTrack("v1", 2'000'000, 720,
                     with_index ? MakeExplicitIndex(first_sequence, 3)
                                : std::shared_ptr<const SegmentIndex>());
  if (!with_index) {
    IndexLocation location;
    location.uri = "https://example.test/video.m3u8";
    video_track.segments = SegmentIndexProvider::Deferred(location);
  }
  video.push_back(std::move(video_track));
  std::vector<Track> audio;
  Track audio_track =
      MakeAudioTrack("a1", 128'000,
                     with_index ? MakeExplicitIndex(first_sequence, 3)
                                : std::shared_ptr<const SegmentIndex>());
  if (!with_index) {
    IndexLocation location;
    location.uri = "https://example.test/audio.m3u8";
    audio_track.segments = SegmentIndexProvider::Deferred(location);
  }
  audio.push_back(std::move(audio_track));
  Period period;
  period.id = PeriodId("hls/d/0");
  period.start = Seconds(0);
  period.duration = TimeDelta::Unknown();
  period.switching_sets = {
      MakeSet("hls/video", penumbra::StreamType::kVideo, std::move(video)),
      MakeSet("hls/audio", penumbra::StreamType::kAudio, std::move(audio))};
  presentation.periods.push_back(Freeze(std::move(period)));
  return presentation;
}

TEST(HlsPlaylistReloadPolicyTest, AppliesChangedAndUnchangedIntervals) {
  HlsPlaylistReloadPolicy policy;
  const Presentation presentation = LivePresentation(0, true);
  const absl::Time start = absl::UnixEpoch();
  const HlsReloadDecision changed =
      policy.OnResponse(presentation, TrackId("v1"), true, start);
  EXPECT_TRUE(changed.reload);
  EXPECT_EQ(changed.delay, absl::Seconds(4));
  EXPECT_FALSE(changed.failover);

  const HlsReloadDecision unchanged = policy.OnResponse(
      presentation, TrackId("v1"), false, start + absl::Seconds(4));
  EXPECT_TRUE(unchanged.reload);
  EXPECT_EQ(unchanged.delay, absl::Seconds(3));
  EXPECT_FALSE(unchanged.failover);

  const HlsReloadDecision stale = policy.OnResponse(
      presentation, TrackId("v1"), false, start + absl::Seconds(9));
  EXPECT_TRUE(stale.reload);
  EXPECT_TRUE(stale.failover);
}

TEST(HlsPlaylistReloadPolicyTest, StaticPlaylistsDoNotReload) {
  HlsPlaylistReloadPolicy policy;
  Presentation presentation = LivePresentation(0, true);
  presentation.type = PresentationType::kStatic;
  presentation.live.reset();
  const HlsReloadDecision decision =
      policy.OnResponse(presentation, TrackId("v1"), true, absl::UnixEpoch());
  EXPECT_FALSE(decision.reload);
  EXPECT_FALSE(decision.failover);
}

TEST(HlsPlaylistReloadPolicyTest, StaleFailoverCanBeDisabled) {
  HlsPlaylistReloadPolicy policy;
  const Presentation presentation = LivePresentation(0, true);
  const absl::Time start = absl::UnixEpoch();
  static_cast<void>(
      policy.OnResponse(presentation, TrackId("v1"), true, start));
  const HlsReloadDecision decision = policy.OnResponse(
      presentation, TrackId("v1"), false, start + absl::Seconds(9), false);
  EXPECT_FALSE(decision.failover);
}

TEST(HlsLiveStartPositionTest, FollowsManifestThenHoldBackThenDefault) {
  Presentation presentation = LivePresentation(0, true);
  LiveInfo& live = *presentation.live;
  live.window_end = Seconds(100);
  presentation.hls->start_offset = Delta(3);
  auto positive = HlsLiveStartPosition(presentation);
  ASSERT_TRUE(positive.ok());
  EXPECT_EQ(*positive, Seconds(3));

  presentation.hls->start_offset = Delta(-4);
  auto negative = HlsLiveStartPosition(presentation);
  ASSERT_TRUE(negative.ok());
  EXPECT_EQ(*negative, Seconds(96));

  presentation.hls->start_offset.reset();
  live.hold_back = Delta(12);
  auto hold_back = HlsLiveStartPosition(presentation);
  ASSERT_TRUE(hold_back.ok());
  EXPECT_EQ(*hold_back, Seconds(88));

  live.hold_back.reset();
  auto default_position = HlsLiveStartPosition(presentation);
  ASSERT_TRUE(default_position.ok());
  EXPECT_EQ(*default_position, Seconds(82));
}

TEST(HlsLiveStartPositionTest, AppliesConfiguredDistanceFromLiveEnd) {
  const Presentation presentation = LivePresentation(0, true);
  auto position = HlsLiveStartPosition(presentation, 5'000);
  ASSERT_TRUE(position.ok());
  EXPECT_EQ(*position, Seconds(7));
}

TEST(HlsPlaylistSelectionTest, RequestsOnlySelectedMediaPlaylists) {
  const Presentation presentation = LivePresentation(0, false);
  const std::vector<std::string> eligible =
      SelectedHlsPlaylistUris(presentation, {TrackId("v1")});
  std::vector<std::string> request_log;
  request_log.reserve(eligible.size());
  for (const std::string& uri : eligible) {
    request_log.push_back(uri);
  }
  ASSERT_EQ(request_log.size(), 1U);
  EXPECT_EQ(request_log[0], "https://example.test/video.m3u8");
}

TEST(HlsPlaylistSelectionTest, SlidesTwoHundredWindowsWithoutStalling) {
  HlsPlaylistReloadPolicy policy;
  auto snapshot = BuildPresentation(LivePresentation(0, true));
  ASSERT_TRUE(snapshot.ok()) << snapshot.status();
  const absl::Time epoch = absl::UnixEpoch();
  static_cast<void>(policy.OnResponse(**snapshot, TrackId("v1"), true, epoch));
  for (std::int64_t refresh = 1; refresh <= 200; ++refresh) {
    auto next = RefreshPresentation(*snapshot, LivePresentation(refresh, true));
    ASSERT_TRUE(next.ok()) << next.status();
    ASSERT_NE((*next)->periods[0]->FindTrack(TrackId("v1")), nullptr);
    EXPECT_EQ((*next)
                  ->periods[0]
                  ->FindTrack(TrackId("v1"))
                  ->segments.Index()
                  ->FirstSequenceNumber(),
              refresh);
    const HlsReloadDecision decision = policy.OnResponse(
        **next, TrackId("v1"), true, epoch + absl::Seconds(refresh * 4));
    EXPECT_TRUE(decision.reload);
    EXPECT_FALSE(decision.failover);
    snapshot = std::move(next);
  }
}

TEST(HlsRefreshIdentityTest, UriMismatchCarriesManifestErrorDetail) {
  auto previous = BuildPresentation(LivePresentation(0, true));
  ASSERT_TRUE(previous.ok());
  Presentation changed = LivePresentation(1, true);
  ExplicitSegmentIndexParameters parameters;
  for (std::int64_t sequence = 1; sequence < 4; ++sequence) {
    Segment segment = MakeSegment(sequence, sequence * 4, 4);
    if (sequence == 1) {
      segment.uri = "https://example.test/replaced.m4s";
    }
    parameters.segments.push_back(std::move(segment));
  }
  auto index = ExplicitSegmentIndex::Create(std::move(parameters));
  ASSERT_TRUE(index.ok());
  Period period = *changed.periods[0];
  SwitchingSet set = *period.switching_sets[0];
  Track track = *set.tracks[0];
  track.segments = SegmentIndexProvider::Resolved(*index);
  set.tracks[0] = Freeze(std::move(track));
  period.switching_sets[0] = Freeze(std::move(set));
  changed.periods[0] = Freeze(std::move(period));

  const auto result = RefreshPresentation(*previous, std::move(changed));
  ASSERT_FALSE(result.ok());
  const auto detail = base::GetErrorDetail(result.status());
  ASSERT_TRUE(detail.ok());
  ASSERT_TRUE(detail->has_value());
  EXPECT_EQ(detail->value().code, base::ErrorCode::kManifestInvalid);
}

}
}
