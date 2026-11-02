#ifndef TEST_UMBRA_MANIFEST_TEST_UTIL_H_
#define TEST_UMBRA_MANIFEST_TEST_UTIL_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/manifest/ids.h"
#include "umbra/manifest/presentation.h"
#include "umbra/manifest/segment.h"
#include "umbra/manifest/segment_index.h"

namespace umbra::manifest::test_data {

class Rng {
 public:
  explicit Rng(std::uint64_t seed) : state_(seed) {}

  std::uint64_t operator()() {
    state_ += 0x9E3779B97F4A7C15ULL;
    std::uint64_t value = state_;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31);
  }

 private:
  std::uint64_t state_;
};

inline InitSection MakeInit(const std::string& uri) {
  InitSection init;
  init.uri = uri;
  return init;
}

inline penumbra::MediaTime Seconds(std::int64_t seconds) {
  return penumbra::MediaTime::FromSeconds(seconds);
}

inline penumbra::TimeDelta Delta(std::int64_t seconds) {
  return penumbra::TimeDelta::FromSeconds(seconds);
}

inline Segment MakeSegment(std::int64_t sequence, std::int64_t start_seconds,
                           std::int64_t duration_seconds) {
  Segment segment;
  segment.sequence_number = sequence;
  segment.start = Seconds(start_seconds);
  segment.duration = Delta(duration_seconds);
  segment.uri = absl::StrCat("https://example.test/seg", sequence, ".m4s");
  return segment;
}

inline std::shared_ptr<const ExplicitSegmentIndex> MakeExplicitIndex(
    std::int64_t first_sequence, std::size_t count,
    std::int64_t duration_seconds = 4) {
  ExplicitSegmentIndexParameters parameters;
  parameters.first_sequence_number = first_sequence;
  for (std::size_t i = 0; i < count; ++i) {
    const auto offset = static_cast<std::int64_t>(i);
    parameters.segments.push_back(MakeSegment(
        first_sequence + offset, (first_sequence + offset) * duration_seconds,
        duration_seconds));
  }
  absl::StatusOr<std::shared_ptr<const ExplicitSegmentIndex>> index =
      ExplicitSegmentIndex::Create(std::move(parameters));
  EXPECT_TRUE(index.ok()) << index.status();
  return index.ok() ? *index : nullptr;
}

inline Track MakeVideoTrack(std::string id, std::int64_t bandwidth,
                            std::int32_t height,
                            std::shared_ptr<const SegmentIndex> index) {
  Track track;
  track.id = TrackId(std::move(id));
  track.bandwidth_bps = bandwidth;
  VideoTrackFormat video;
  video.width = height * 16 / 9;
  video.height = height;
  video.frame_rate = penumbra::Rational::Of(30, 1);
  video.bit_depth = 8;
  video.range = VideoRange::kSdr;
  track.format = video;
  track.segments = SegmentIndexProvider::Resolved(std::move(index));
  return track;
}

inline Track MakeAudioTrack(std::string id, std::int64_t bandwidth,
                            std::shared_ptr<const SegmentIndex> index) {
  Track track;
  track.id = TrackId(std::move(id));
  track.bandwidth_bps = bandwidth;
  AudioTrackFormat audio;
  audio.sample_rate = 48'000;
  audio.channels.channel_count = 2;
  track.format = audio;
  track.segments = SegmentIndexProvider::Resolved(std::move(index));
  return track;
}

inline std::shared_ptr<const SwitchingSet> MakeSet(std::string id,
                                                   penumbra::StreamType type,
                                                   std::vector<Track> tracks) {
  SwitchingSet set;
  set.id = SwitchingSetId(std::move(id));
  set.type = type;
  for (Track& track : tracks) {
    set.tracks.push_back(Freeze(std::move(track)));
  }
  return Freeze(std::move(set));
}

inline std::shared_ptr<const Period> MakePeriod(
    std::string id, std::int64_t start_seconds, std::int64_t duration_seconds,
    std::vector<std::shared_ptr<const SwitchingSet>> sets) {
  Period period;
  period.id = PeriodId(std::move(id));
  period.start = Seconds(start_seconds);
  period.duration = Delta(duration_seconds);
  period.switching_sets = std::move(sets);
  return Freeze(std::move(period));
}

inline Presentation MakeVodPresentation(std::size_t segments = 5) {
  Presentation presentation;
  presentation.protocol = Protocol::kDash;
  presentation.type = PresentationType::kStatic;
  presentation.duration = Delta(static_cast<std::int64_t>(segments) * 4);
  std::vector<Track> video;
  video.push_back(
      MakeVideoTrack("v1", 2'000'000, 720, MakeExplicitIndex(0, segments)));
  video.push_back(
      MakeVideoTrack("v2", 5'000'000, 1080, MakeExplicitIndex(0, segments)));
  std::vector<Track> audio;
  audio.push_back(
      MakeAudioTrack("a1", 128'000, MakeExplicitIndex(0, segments)));
  presentation.periods.push_back(MakePeriod(
      "p0", 0, static_cast<std::int64_t>(segments) * 4,
      {MakeSet("video", penumbra::StreamType::kVideo, std::move(video)),
       MakeSet("audio", penumbra::StreamType::kAudio, std::move(audio))}));
  return presentation;
}

}

#endif
