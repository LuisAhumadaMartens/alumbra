#include <cmath>
#include <limits>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port_types.h"
#include "penumbra_conformance/conformance.h"
#include "penumbra_conformance/port_harness.h"

namespace penumbra::conformance {
namespace {

PlayerConfig PunchOutConfig() {
  auto video = VideoStream();
  video.output_mode = OutputMode::kPunchOut;
  PlayerConfig config;
  config.streams = {video, AudioStream()};
  return config;
}

}

TEST_P(OutputConformance, BoundsApplyToPunchOutVideoTracksOnly) {
  ALUMBRA_PORT_REQUIRE(Capability::kPunchOutOutput);
  ASSERT_TRUE(Create(PunchOutConfig()).ok());
  Run([this] {
    EXPECT_TRUE(CurrentPlayer()
                    .SetBounds(kVideoTrack, {.width = 640, .height = 360})
                    .ok());
    EXPECT_TRUE(CurrentPlayer().SetBounds(kVideoTrack, {}).ok());
    EXPECT_EQ(CurrentPlayer()
                  .SetBounds(kVideoTrack, {.width = -1, .height = 360})
                  .code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(CurrentPlayer()
                  .SetBounds(kVideoTrack, {.width = 640, .height = -1})
                  .code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(CurrentPlayer()
                  .SetBounds(kAudioTrack, {.width = 640, .height = 360})
                  .code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(
        CurrentPlayer().SetBounds(99, {.width = 640, .height = 360}).code(),
        absl::StatusCode::kInvalidArgument);
  });
}

TEST_P(OutputConformance, PunchOutTracksHaveNoCurrentFrame) {
  ALUMBRA_PORT_REQUIRE(Capability::kPunchOutOutput);
  ASSERT_TRUE(Create(PunchOutConfig()).ok());
  Run([this] {
    EXPECT_EQ(CurrentPlayer().GetCurrentFrame(kVideoTrack).status().code(),
              absl::StatusCode::kInvalidArgument);
  });
}

TEST_P(OutputConformance, TextureVideoTracksReportAnOptionalCurrentFrame) {
  ALUMBRA_PORT_REQUIRE(Capability::kTextureOutput);
  ASSERT_TRUE(Create().ok());
  Run([this] {
    EXPECT_TRUE(CurrentPlayer().GetCurrentFrame(kVideoTrack).ok());
    EXPECT_EQ(CurrentPlayer().GetCurrentFrame(kAudioTrack).status().code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(CurrentPlayer().GetCurrentFrame(99).status().code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(CurrentPlayer()
                  .SetBounds(kVideoTrack, {.width = 1, .height = 1})
                  .code(),
              absl::StatusCode::kInvalidArgument);
  });
}

TEST_P(OutputConformance, PlaybackRateRejectsNegativeAndNonFiniteValues) {
  ASSERT_TRUE(Create().ok());
  Run([this] {
    EXPECT_EQ(CurrentPlayer().SetPlaybackRate(-1.0).code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(CurrentPlayer().SetPlaybackRate(std::nan("")).code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(CurrentPlayer()
                  .SetPlaybackRate(std::numeric_limits<double>::infinity())
                  .code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_TRUE(CurrentPlayer().SetPlaybackRate(0.0).ok());
    EXPECT_TRUE(CurrentPlayer().SetPlaybackRate(1.0).ok());
  });
}

TEST_P(OutputConformance, VolumeIsANormalizedLinearGain) {
  ASSERT_TRUE(Create().ok());
  Run([this] {
    EXPECT_EQ(CurrentPlayer().SetVolume(-0.1).code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(CurrentPlayer().SetVolume(1.5).code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(CurrentPlayer().SetVolume(std::nan("")).code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_TRUE(CurrentPlayer().SetVolume(0.0).ok());
    EXPECT_TRUE(CurrentPlayer().SetVolume(0.5).ok());
    EXPECT_TRUE(CurrentPlayer().SetVolume(1.0).ok());
  });
}

TEST_P(OutputConformance, SnapshotStartsWithZeroCounters) {
  ASSERT_TRUE(Create().ok());
  absl::StatusOr<PlaybackSnapshot> snapshot;
  Run([&] { snapshot = CurrentPlayer().GetSnapshot(); });
  ASSERT_TRUE(snapshot.ok()) << snapshot.status();
  EXPECT_EQ(snapshot->frames.decoded, 0U);
  EXPECT_EQ(snapshot->frames.dropped, 0U);
  EXPECT_EQ(snapshot->frames.corrupted, 0U);
}

}
