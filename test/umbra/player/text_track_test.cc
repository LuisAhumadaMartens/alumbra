#include "umbra/player/text_track.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"

namespace umbra::player {
namespace {

TextCue Cue(uint64_t id, double start, double end) {
  TextCue cue;
  cue.id = id;
  cue.start_time = start;
  cue.end_time = end;
  cue.runs.push_back({.text = std::to_string(id),
                      .voice = "",
                      .language = "",
                      .bold = false,
                      .italic = false,
                      .underline = false,
                      .color = ""});
  return cue;
}

TEST(TextTrackTest, MatchesHalfOpenCueIntervalsAndIgnoresZeroLength) {
  TextTrack track(TextTrackKind::kSubtitles, "English", "en", 4096);
  track.set_mode(TextTrackMode::kHidden);
  ASSERT_TRUE(track.AddCue(Cue(2, 1.0, 3.0)));
  ASSERT_TRUE(track.AddCue(Cue(1, 0.0, 2.0)));
  ASSERT_TRUE(track.AddCue(Cue(3, 2.0, 2.0)));
  track.Update(2.0);
  const auto active = track.active_cues();
  ASSERT_EQ(active.size(), 1U);
  EXPECT_EQ(active.front().id, 2U);
}

TEST(TextTrackTest, DisabledTrackHasNoActiveCuesAndModeChangesNotify) {
  TextTrack track(TextTrackKind::kCaptions, "", "en", 4096);
  int changes = 0;
  auto subscription = track.AddCueChangeListener([&changes] { ++changes; });
  ASSERT_TRUE(track.AddCue(Cue(1, 1.0, 2.0)));
  track.Update(1.5);
  EXPECT_TRUE(track.active_cues().empty());
  track.set_mode(TextTrackMode::kHidden);
  ASSERT_EQ(track.active_cues().size(), 1U);
  EXPECT_EQ(changes, 1);
  track.set_mode(TextTrackMode::kDisabled);
  EXPECT_TRUE(track.active_cues().empty());
  EXPECT_EQ(changes, 2);
}

TEST(TextTrackListTest, AddsAndRemovesTracksAndSignalsChanges) {
  TextTrackList tracks;
  std::vector<TextTrackListEventType> events;
  auto subscription = tracks.AddEventListener(
      [&events](TextTrackListEventType event) { events.push_back(event); });
  auto track =
      std::make_shared<TextTrack>(TextTrackKind::kMetadata, "", "", 4096);
  tracks.Add(track);
  EXPECT_EQ(tracks.length(), 1U);
  EXPECT_EQ(tracks.item(0), track);
  EXPECT_TRUE(tracks.Remove(track->id()));
  EXPECT_EQ(events, (std::vector<TextTrackListEventType>{
                        TextTrackListEventType::kAddTrack,
                        TextTrackListEventType::kChange,
                        TextTrackListEventType::kRemoveTrack,
                        TextTrackListEventType::kChange}));
  EXPECT_EQ(tracks.length(), 0U);
}

TEST(TextTrackTest, EvictsExpiredCuesBeforeRejectingNewStorage) {
  TextTrack track(TextTrackKind::kSubtitles, "", "", 1200);
  TextCue first = Cue(1, 0.0, 1.0);
  first.runs.front().text.assign(400, 'a');
  ASSERT_TRUE(track.AddCue(std::move(first)));
  track.Update(1.0);
  TextCue second = Cue(2, 1.0, 2.0);
  second.runs.front().text.assign(400, 'b');
  ASSERT_TRUE(track.AddCue(std::move(second)));
  EXPECT_LE(track.memory_usage_bytes(), 1200U);
  ASSERT_EQ(track.cues().size(), 1U);
  EXPECT_EQ(track.cues().front().id, 2U);
}

TEST(TextTrackTest, RejectsCueWhenBudgetCannotHoldIt) {
  TextTrack track(TextTrackKind::kSubtitles, "", "", 64);
  EXPECT_FALSE(track.AddCue(Cue(1, 0.0, 1.0)));
  EXPECT_EQ(track.memory_usage_bytes(), 0U);
}

}
}
