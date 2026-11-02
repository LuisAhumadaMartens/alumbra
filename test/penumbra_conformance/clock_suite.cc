#include <cstddef>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port_types.h"
#include "penumbra_conformance/conformance.h"
#include "penumbra_conformance/port_harness.h"

namespace penumbra::conformance {
namespace {

constexpr int kSteps = 10;

}

TEST_P(ClockConformance, MediaTimeNeverDecreasesWhilePlaying) {
  ALUMBRA_PORT_REQUIRE(Capability::kClockAdvance);
  ASSERT_TRUE(Create().ok());
  ASSERT_TRUE(DoSeek(2, MediaTime::FromSeconds(10)).ok());
  std::vector<MediaTime> positions;
  for (int step = 0; step < kSteps; ++step) {
    absl::StatusOr<PlaybackSnapshot> snapshot;
    Run([&] {
      EXPECT_TRUE(Harness()
                      .AdvancePlayback(CurrentPlayer(), absl::Milliseconds(100))
                      .ok());
      snapshot = CurrentPlayer().GetSnapshot();
    });
    ASSERT_TRUE(snapshot.ok()) << snapshot.status();
    ASSERT_TRUE(snapshot->media_time.is_finite());
    positions.push_back(snapshot->media_time);
  }
  for (size_t i = 1; i < positions.size(); ++i) {
    EXPECT_GE(positions[i], positions[i - 1]) << "step " << i;
  }
  EXPECT_GT(positions.back(), MediaTime::FromSeconds(10));
}

TEST_P(ClockConformance, PausedPlayerDoesNotAdvance) {
  ALUMBRA_PORT_REQUIRE(Capability::kClockAdvance);
  ASSERT_TRUE(Create().ok());
  ASSERT_TRUE(DoSeek(2, MediaTime::FromSeconds(3)).ok());
  absl::StatusOr<PlaybackSnapshot> snapshot;
  Run([&] {
    EXPECT_TRUE(CurrentPlayer().SetPlaybackRate(0).ok());
    EXPECT_TRUE(
        Harness().AdvancePlayback(CurrentPlayer(), absl::Seconds(1)).ok());
    snapshot = CurrentPlayer().GetSnapshot();
  });
  ASSERT_TRUE(snapshot.ok()) << snapshot.status();
  EXPECT_EQ(snapshot->media_time, MediaTime::FromSeconds(3));
}

TEST_P(ClockConformance, SeekAfterPlaybackResetsTheClock) {
  ALUMBRA_PORT_REQUIRE(Capability::kClockAdvance);
  ASSERT_TRUE(Create().ok());
  Run([this] {
    EXPECT_TRUE(
        Harness().AdvancePlayback(CurrentPlayer(), absl::Seconds(2)).ok());
  });
  ASSERT_TRUE(DoSeek(2, MediaTime::FromSeconds(1)).ok());
  absl::StatusOr<PlaybackSnapshot> snapshot;
  Run([&] { snapshot = CurrentPlayer().GetSnapshot(); });
  ASSERT_TRUE(snapshot.ok()) << snapshot.status();
  EXPECT_EQ(snapshot->media_time, MediaTime::FromSeconds(1));
}

TEST_P(ClockConformance, FlushAfterPlaybackPreservesTheClock) {
  ALUMBRA_PORT_REQUIRE(Capability::kClockAdvance);
  ASSERT_TRUE(Create().ok());
  ASSERT_TRUE(DoSeek(2, MediaTime::FromSeconds(4)).ok());
  absl::StatusOr<PlaybackSnapshot> before;
  Run([&] {
    EXPECT_TRUE(
        Harness().AdvancePlayback(CurrentPlayer(), absl::Seconds(1)).ok());
    before = CurrentPlayer().GetSnapshot();
  });
  ASSERT_TRUE(before.ok()) << before.status();
  ASSERT_TRUE(DoFlush(3).ok());
  absl::StatusOr<PlaybackSnapshot> after;
  Run([&] { after = CurrentPlayer().GetSnapshot(); });
  ASSERT_TRUE(after.ok()) << after.status();
  EXPECT_EQ(after->media_time, before->media_time);
}

}
