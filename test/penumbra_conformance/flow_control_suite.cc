#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra_conformance/conformance.h"
#include "penumbra_conformance/port_harness.h"

namespace penumbra::conformance {
namespace {

MediaFragment MakeFragment(std::vector<uint32_t> tracks) {
  MediaFragment fragment;
  fragment.data = SharedBytes::Adopt({1, 2, 3, 4});
  fragment.track_ids = std::move(tracks);
  fragment.presentation_time = MediaTime::Zero();
  fragment.duration = TimeDelta::FromSeconds(2);
  return fragment;
}

}

TEST_P(FlowControlConformance, WriteWithoutGrantIsAFailedPrecondition) {
  ALUMBRA_PORT_REQUIRE(Capability::kDataRequestHook);
  ASSERT_TRUE(Create().ok());
  const auto sample = MakeSample(kVideoTrack, 0);
  Run([&] {
    EXPECT_EQ(CurrentPlayer()
                  .WriteSamples(Ticket(), kVideoTrack, {&sample, 1})
                  .code(),
              absl::StatusCode::kFailedPrecondition);
  });
}

TEST_P(FlowControlConformance, GrantNamesAKnownTrackWithAPositiveMaximum) {
  ALUMBRA_PORT_REQUIRE(Capability::kDataRequestHook);
  ASSERT_TRUE(Create().ok());
  ASSERT_TRUE(Grant(kVideoTrack, 3));
  Run([this] {
    EXPECT_TRUE(Harness().RequestData(CurrentPlayer(), kAudioTrack, 2).ok());
  });
  size_t seen = 0;
  for (const auto& event : Receiver().Events()) {
    if (const auto* needs = std::get_if<NeedsData>(&event.detail)) {
      EXPECT_GT(needs->max_samples, 0U);
      EXPECT_TRUE(needs->track_id == kVideoTrack ||
                  needs->track_id == kAudioTrack);
      ++seen;
    }
  }
  EXPECT_EQ(seen, 2U);
}

TEST_P(FlowControlConformance, OneGrantIsConsumedByOneWrite) {
  ALUMBRA_PORT_REQUIRE(Capability::kDataRequestHook);
  ASSERT_TRUE(Create().ok());
  ASSERT_TRUE(Grant(kVideoTrack, 2));
  const auto sample = MakeSample(kVideoTrack, 0);
  Run([&] {
    EXPECT_TRUE(
        CurrentPlayer().WriteSamples(Ticket(), kVideoTrack, {&sample, 1}).ok());
    EXPECT_EQ(CurrentPlayer()
                  .WriteSamples(Ticket(), kVideoTrack, {&sample, 1})
                  .code(),
              absl::StatusCode::kFailedPrecondition);
  });
}

TEST_P(FlowControlConformance, InvalidWritesDoNotConsumeTheGrant) {
  ALUMBRA_PORT_REQUIRE(Capability::kDataRequestHook);
  ASSERT_TRUE(Create().ok());
  ASSERT_TRUE(Grant(kVideoTrack, 1));
  const std::array samples{MakeSample(kVideoTrack, 0),
                           MakeSample(kVideoTrack, 1)};
  const auto wrong_track = MakeSample(kAudioTrack, 0);
  Run([&] {
    EXPECT_EQ(
        CurrentPlayer().WriteSamples(Ticket(), kVideoTrack, samples).code(),
        absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(CurrentPlayer().WriteSamples(Ticket(), kVideoTrack, {}).code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(CurrentPlayer()
                  .WriteSamples(Ticket(), kVideoTrack, {&wrong_track, 1})
                  .code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(
        CurrentPlayer().WriteSamples(Ticket(), 99, {samples.data(), 1}).code(),
        absl::StatusCode::kInvalidArgument);
    EXPECT_TRUE(CurrentPlayer()
                    .WriteSamples(Ticket(), kVideoTrack, {samples.data(), 1})
                    .ok());
  });
}

TEST_P(FlowControlConformance, EndOfStreamIsPerTrackAndEventuallyAggregate) {
  ALUMBRA_PORT_REQUIRE(Capability::kDataRequestHook);
  ASSERT_TRUE(Create().ok());
  Run([this] {
    EXPECT_EQ(CurrentPlayer().EndOfStream(Ticket(), kVideoTrack).code(),
              absl::StatusCode::kFailedPrecondition);
  });
  ASSERT_TRUE(Grant(kVideoTrack));
  const auto sample = MakeSample(kVideoTrack, 0);
  Run([&] {
    EXPECT_TRUE(CurrentPlayer().EndOfStream(Ticket(), kVideoTrack).ok());
    EXPECT_EQ(CurrentPlayer().EndOfStream(Ticket(), kVideoTrack).code(),
              absl::StatusCode::kFailedPrecondition);
    EXPECT_EQ(CurrentPlayer()
                  .WriteSamples(Ticket(), kVideoTrack, {&sample, 1})
                  .code(),
              absl::StatusCode::kFailedPrecondition);
    EXPECT_FALSE(Harness().RequestData(CurrentPlayer(), kVideoTrack, 1).ok());
  });
  ASSERT_TRUE(Grant(kAudioTrack));
  Run([this] {
    EXPECT_TRUE(CurrentPlayer().EndOfStream(Ticket(), kAudioTrack).ok());
  });

  std::optional<size_t> video_end;
  std::optional<size_t> audio_end;
  std::vector<size_t> aggregate_ends;
  const auto events = Receiver().Events();
  for (size_t i = 0; i < events.size(); ++i) {
    const auto* state = std::get_if<StateChanged>(&events[i].detail);
    if (state == nullptr || state->state != PlayerState::kEndOfStream) {
      continue;
    }
    if (!state->track_id.has_value()) {
      aggregate_ends.push_back(i);
    } else if (*state->track_id == kVideoTrack) {
      video_end = i;
    } else if (*state->track_id == kAudioTrack) {
      audio_end = i;
    }
  }
  ASSERT_TRUE(video_end.has_value());
  ASSERT_TRUE(audio_end.has_value());
  ASSERT_EQ(aggregate_ends.size(), 1U);
  EXPECT_GT(aggregate_ends[0], *video_end);
  EXPECT_GT(aggregate_ends[0], *audio_end);
}

TEST_P(FlowControlConformance, FragmentPlayersTakeOneGrantPerFragment) {
  ALUMBRA_PORT_REQUIRE(Capability::kFragmentInput);
  ALUMBRA_PORT_REQUIRE(Capability::kDataRequestHook);
  auto config = DefaultPlayerConfig();
  config.input_mode = InputMode::kFragments;
  ASSERT_TRUE(Create(std::move(config)).ok());
  ASSERT_TRUE(Grant(kVideoTrack));
  const auto sample = MakeSample(kVideoTrack, 0);
  const auto fragment = MakeFragment({kVideoTrack, kAudioTrack});
  Run([&] {
    EXPECT_FALSE(
        CurrentPlayer().WriteSamples(Ticket(), kVideoTrack, {&sample, 1}).ok());
    EXPECT_EQ(
        CurrentPlayer().WriteFragment(Ticket(), kAudioTrack, fragment).code(),
        absl::StatusCode::kFailedPrecondition);
    EXPECT_EQ(CurrentPlayer().WriteFragment(Ticket(), 99, fragment).code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(CurrentPlayer()
                  .WriteFragment(Ticket(), kVideoTrack, MakeFragment({}))
                  .code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_TRUE(
        CurrentPlayer().WriteFragment(Ticket(), kVideoTrack, fragment).ok());
    EXPECT_EQ(
        CurrentPlayer().WriteFragment(Ticket(), kVideoTrack, fragment).code(),
        absl::StatusCode::kFailedPrecondition);
  });
}

TEST_P(FlowControlConformance, SamplePlayersRejectFragments) {
  ALUMBRA_PORT_REQUIRE(Capability::kDataRequestHook);
  ASSERT_TRUE(Create().ok());
  ASSERT_TRUE(Grant(kVideoTrack));
  const auto fragment = MakeFragment({kVideoTrack});
  Run([&] {
    EXPECT_EQ(
        CurrentPlayer().WriteFragment(Ticket(), kVideoTrack, fragment).code(),
        absl::StatusCode::kInvalidArgument);
  });
}

}
