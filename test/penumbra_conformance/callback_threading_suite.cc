#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"
#include "penumbra/interface/player.h"
#include "penumbra_conformance/conformance.h"
#include "penumbra_conformance/port_harness.h"

namespace penumbra::conformance {

TEST_P(CallbackThreadingConformance, EventsAreDeliveredInsideACallbackScope) {
  ALUMBRA_PORT_REQUIRE(Capability::kDataRequestHook);
  auto raw = std::make_shared<RecordingReceiver>(Control());
  std::unique_ptr<Player> raw_player;
  Run([&] {
    auto result =
        Harness().UnderTest().CreatePlayer(DefaultPlayerConfig(), raw);
    ASSERT_TRUE(result.ok()) << result.status();
    raw_player = std::move(*result);
    EXPECT_TRUE(Harness().RequestData(*raw_player, kVideoTrack, 1).ok());
    EXPECT_TRUE(Harness().RequestData(*raw_player, kAudioTrack, 1).ok());
    EXPECT_TRUE(raw_player->EndOfStream(1, kVideoTrack).ok());
    EXPECT_TRUE(raw_player->Stop().ok());
  });
  EXPECT_GE(raw->Events().size(), 3U);
  EXPECT_EQ(raw->OutsideCallbackScope(), 0U);
}

TEST_P(CallbackThreadingConformance, NoEventIsDeliveredAfterStop) {
  ALUMBRA_PORT_REQUIRE(Capability::kDataRequestHook);
  auto raw = std::make_shared<RecordingReceiver>(Control());
  std::unique_ptr<Player> raw_player;
  Run([&] {
    auto result =
        Harness().UnderTest().CreatePlayer(DefaultPlayerConfig(), raw);
    ASSERT_TRUE(result.ok()) << result.status();
    raw_player = std::move(*result);
    EXPECT_TRUE(raw_player->Stop().ok());
  });
  Run([&] {
    EXPECT_FALSE(Harness().RequestData(*raw_player, kVideoTrack, 1).ok());
    EXPECT_FALSE(raw_player->EndOfStream(1, kVideoTrack).ok());
  });
  Settle();
  EXPECT_TRUE(raw->Events().empty());
}

TEST_P(CallbackThreadingConformance, DispatchedEventsRunOnTheControlSequence) {
  ALUMBRA_PORT_REQUIRE(Capability::kDataRequestHook);
  ASSERT_TRUE(Create().ok());
  ASSERT_TRUE(Grant(kVideoTrack));
  ASSERT_TRUE(Grant(kAudioTrack));
  Run([this] {
    EXPECT_TRUE(CurrentPlayer().EndOfStream(Ticket(), kVideoTrack).ok());
  });
  EXPECT_EQ(Receiver().OffControlSequence(), 0U);
  EXPECT_EQ(Receiver().OutsideCallbackScope(), 0U);
  EXPECT_TRUE(Dispatcher().GetDeliveryStatus().ok());
}

TEST_P(CallbackThreadingConformance, DeliveryFailureIsObservable) {
  ALUMBRA_PORT_REQUIRE(Capability::kDataRequestHook);
  class FailingReceiver final : public PlayerCallbacks {
   public:
    absl::Status OnEvent([[maybe_unused]] PlayerEvent event) override {
      return absl::AbortedError("receiver rejected the event");
    }
  };
  auto failing = std::make_shared<FailingReceiver>();
  std::unique_ptr<Player> failing_player;
  absl::Status status;
  Run([&] {
    auto result =
        Harness().UnderTest().CreatePlayer(DefaultPlayerConfig(), failing);
    ASSERT_TRUE(result.ok()) << result.status();
    failing_player = std::move(*result);
    status = Harness().RequestData(*failing_player, kVideoTrack, 1);
    EXPECT_TRUE(failing_player->Stop().ok());
  });
  EXPECT_EQ(status.code(), absl::StatusCode::kAborted);
}

}
