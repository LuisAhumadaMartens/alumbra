#include "umbra/observability/statistics_model.h"

#include <cstdint>

#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "umbra/base/test_support/fake_tick_clock.h"
#include "umbra/observability/metrics.h"
#include "umbra/player/statistics.h"

namespace umbra::observability {
namespace {

MetricsRecord At(MetricsEvent event, std::int64_t timestamp_ms) {
  return {.event = event, .timestamp_ms = timestamp_ms};
}

TEST(StatisticsModelTest, AggregatesDocumentedStartupStallAndFailureValues) {
  StatisticsModel model;
  model.OnMetricsEvent(At(PlaybackStart{1200}, 1200));
  model.OnMetricsEvent(At(PlaybackStall{500}, 2500));
  model.OnMetricsEvent(At(PlaybackSessionEnded{10000}, 10000));
  model.OnMetricsEvent(At(PlaybackStart{800}, 12000));
  model.OnMetricsEvent(At(PlaybackSessionEnded{10000}, 14000));
  model.OnMetricsEvent(
      At(PlaybackFailed{.error_code = 7, .during_startup = true}, 15000));
  model.OnMetricsEvent(
      At(PlaybackFailed{.error_code = 9, .during_startup = false}, 15001));
  model.OnMetricsEvent(At(PlaybackSessionEnded{0}, 15000));
  model.OnMetricsEvent(
      At(PlaybackFailed{.error_code = 8, .during_startup = false}, 20000));
  model.OnMetricsEvent(At(PlaybackSessionEnded{10000}, 25000));

  const player::StatisticsSnapshot snapshot = model.GetSnapshot();
  ASSERT_TRUE(snapshot.average_initial_startup_time_s.value.has_value());
  EXPECT_DOUBLE_EQ(*snapshot.average_initial_startup_time_s.value, 1.0);
  ASSERT_TRUE(snapshot.startup_failures.value.has_value());
  EXPECT_EQ(*snapshot.startup_failures.value, 1U);
  ASSERT_TRUE(snapshot.playback_failure_percentage.value.has_value());
  EXPECT_DOUBLE_EQ(*snapshot.playback_failure_percentage.value, 50.0);
  ASSERT_TRUE(snapshot.average_playback_stalled_count.value.has_value());
  EXPECT_DOUBLE_EQ(*snapshot.average_playback_stalled_count.value, 0.25);
  ASSERT_TRUE(snapshot.average_stalled_time_percentage.value.has_value());
  EXPECT_DOUBLE_EQ(*snapshot.average_stalled_time_percentage.value,
                   500.0 / 300.0);
}

TEST(StatisticsModelTest, FakeClockProducesStartupAndStallDurations) {
  base::FakeTickClock clock;
  StatisticsModel model;
  clock.Advance(absl::Milliseconds(1500));
  const std::int64_t startup_ms =
      absl::ToInt64Milliseconds(clock.NowTicks().SinceOrigin());
  model.OnMetricsEvent(At(PlaybackStart{startup_ms}, startup_ms));
  clock.Advance(absl::Milliseconds(12000));
  clock.Advance(absl::Milliseconds(750));
  const std::int64_t stall_ms = 750;
  model.OnMetricsEvent(
      At(PlaybackStall{stall_ms},
         absl::ToInt64Milliseconds(clock.NowTicks().SinceOrigin())));
  model.OnMetricsEvent(At(PlaybackSessionEnded{12000}, 14250));

  const auto snapshot = model.GetSnapshot();
  EXPECT_DOUBLE_EQ(*snapshot.average_initial_startup_time_s.value, 1.5);
  EXPECT_DOUBLE_EQ(*snapshot.average_stalled_time_percentage.value, 6.25);
}

TEST(StatisticsModelTest, CalculatesTimeWeightedAndUnweightedBitrate) {
  StatisticsModel model;
  model.OnMetricsEvent(
      At(BitrateSample{.bits_played = 8000, .media_time_ms = 1000}, 1000));
  model.OnMetricsEvent(
      At(BitrateSample{.bits_played = 36000, .media_time_ms = 3000}, 4000));

  const player::StatisticsSnapshot snapshot = model.GetSnapshot();
  ASSERT_TRUE(snapshot.average_playback_bitrate_kbps.value.has_value());
  EXPECT_DOUBLE_EQ(*snapshot.average_playback_bitrate_kbps.value, 11.0);
  ASSERT_TRUE(snapshot.average_segment_bitrate_bps.value.has_value());
  EXPECT_DOUBLE_EQ(*snapshot.average_segment_bitrate_bps.value, 10000.0);
}

TEST(StatisticsModelTest, CountsRenditionChangesAndDroppedFrames) {
  StatisticsModel model;
  model.OnMetricsEvent(At(
      RenditionUpdate{.from_bitrate_bps = 1000000, .to_bitrate_bps = 1000000},
      100));
  model.OnMetricsEvent(At(
      RenditionUpdate{.from_bitrate_bps = 1000000, .to_bitrate_bps = 2000000},
      200));
  model.OnMetricsEvent(
      At(RenditionUpdate{.from_bitrate_bps = 2000000, .to_bitrate_bps = 500000},
         300));
  model.OnMetricsEvent(At(FramesDropped{3}, 400));
  model.OnMetricsEvent(At(FramesDropped{2}, 500));
  model.OnMetricsEvent(At(LiveLatencySample{1250}, 600));

  const player::StatisticsSnapshot snapshot = model.GetSnapshot();
  ASSERT_TRUE(snapshot.rendition_switch_count.value.has_value());
  EXPECT_EQ(*snapshot.rendition_switch_count.value, 2U);
  ASSERT_TRUE(snapshot.dropped_frames.value.has_value());
  EXPECT_EQ(*snapshot.dropped_frames.value, 5U);
  ASSERT_TRUE(snapshot.live_latency_ms.value.has_value());
  EXPECT_DOUBLE_EQ(*snapshot.live_latency_ms.value, 1250.0);
}

TEST(StatisticsModelTest, DisabledCollectionDoesNotUpdateSnapshot) {
  StatisticsModel model(false);
  model.OnMetricsEvent(At(PlaybackStart{100}, 100));
  EXPECT_FALSE(model.GetSnapshot().average_initial_startup_time_s.value);
  model.SetEnabled(true);
  model.OnMetricsEvent(At(PlaybackStart{200}, 200));
  ASSERT_TRUE(model.GetSnapshot().average_initial_startup_time_s.value);
  model.SetEnabled(false);
  EXPECT_FALSE(model.GetSnapshot().average_initial_startup_time_s.value);
}

TEST(StatisticsModelTest, UnstableDiagnosticsRequireExplicitOptIn) {
  StatisticsModel model;
  model.OnMetricsEvent(At(PlaybackStart{100}, 1234));
  EXPECT_FALSE(model.GetSnapshot().unstable_debug.has_value());
  const auto snapshot = model.GetSnapshot(true);
  ASSERT_TRUE(snapshot.unstable_debug.has_value());
  EXPECT_EQ(*snapshot.unstable_debug->events_received.value, 1U);
  EXPECT_EQ(*snapshot.unstable_debug->last_event_timestamp_ms.value, 1234);
  EXPECT_EQ(snapshot.unstable_debug->events_received.stability,
            player::MetricStability::kUnstable);
}

}
}
