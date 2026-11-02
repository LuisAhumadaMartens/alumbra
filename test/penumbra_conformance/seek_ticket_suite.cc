#include <limits>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port_types.h"
#include "penumbra_conformance/conformance.h"
#include "penumbra_conformance/port_harness.h"

namespace penumbra::conformance {

TEST_P(SeekTicketConformance, SeekRequiresAStrictlyIncreasingTicket) {
  ASSERT_TRUE(Create().ok());
  Run([this] {
    EXPECT_EQ(CurrentPlayer().Seek(Ticket(), MediaTime::Zero()).code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(CurrentPlayer().Seek(0, MediaTime::Zero()).code(),
              absl::StatusCode::kInvalidArgument);
  });
  ASSERT_TRUE(DoSeek(2, MediaTime::Zero()).ok());
  Run([this] {
    EXPECT_EQ(CurrentPlayer().Seek(2, MediaTime::Zero()).code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(CurrentPlayer().Seek(1, MediaTime::Zero()).code(),
              absl::StatusCode::kInvalidArgument);
  });
}

TEST_P(SeekTicketConformance, FlushRequiresAStrictlyIncreasingTicket) {
  ASSERT_TRUE(Create().ok());
  Run([this] {
    EXPECT_EQ(CurrentPlayer().Flush(Ticket()).code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(CurrentPlayer().Flush(0).code(),
              absl::StatusCode::kInvalidArgument);
  });
  ASSERT_TRUE(DoFlush(2).ok());
  Run([this] {
    EXPECT_EQ(CurrentPlayer().Flush(2).code(),
              absl::StatusCode::kInvalidArgument);
  });
}

TEST_P(SeekTicketConformance, FailedSeekLeavesTheGenerationUnchanged) {
  ASSERT_TRUE(Create().ok());
  Run([this] {
    EXPECT_EQ(CurrentPlayer().Seek(5, MediaTime::Unknown()).code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(CurrentPlayer().Seek(5, MediaTime::Infinite()).code(),
              absl::StatusCode::kInvalidArgument);
  });
  EXPECT_TRUE(DoSeek(2, MediaTime::Zero()).ok());
}

TEST_P(SeekTicketConformance, SeekResetsTheClockAndFlushPreservesIt) {
  ASSERT_TRUE(Create().ok());
  const auto target = MediaTime::FromSeconds(5);
  ASSERT_TRUE(DoSeek(2, target).ok());
  absl::StatusOr<PlaybackSnapshot> snapshot;
  Run([&] { snapshot = CurrentPlayer().GetSnapshot(); });
  ASSERT_TRUE(snapshot.ok()) << snapshot.status();
  EXPECT_EQ(snapshot->media_time, target);
  ASSERT_TRUE(DoFlush(3).ok());
  Run([&] { snapshot = CurrentPlayer().GetSnapshot(); });
  ASSERT_TRUE(snapshot.ok()) << snapshot.status();
  EXPECT_EQ(snapshot->media_time, target);
}

TEST_P(SeekTicketConformance, CountersResetWithEachGeneration) {
  ASSERT_TRUE(Create().ok());
  ASSERT_TRUE(DoSeek(2, MediaTime::Zero()).ok());
  absl::StatusOr<PlaybackSnapshot> snapshot;
  Run([&] { snapshot = CurrentPlayer().GetSnapshot(); });
  ASSERT_TRUE(snapshot.ok()) << snapshot.status();
  EXPECT_EQ(snapshot->frames.decoded, 0U);
  EXPECT_EQ(snapshot->frames.dropped, 0U);
  EXPECT_EQ(snapshot->frames.corrupted, 0U);
}

TEST_P(SeekTicketConformance, StaleWriteIsDiscardedWithoutConsumingTheGrant) {
  ALUMBRA_PORT_REQUIRE(Capability::kDataRequestHook);
  ASSERT_TRUE(Create().ok());
  ASSERT_TRUE(DoSeek(2, MediaTime::Zero()).ok());
  ASSERT_TRUE(Grant(kVideoTrack)) << "grant for the new ticket never arrived";
  const auto sample = MakeSample(kVideoTrack, 0);
  Run([&] {
    EXPECT_TRUE(
        CurrentPlayer().WriteSamples(1, kVideoTrack, {&sample, 1}).ok());
    EXPECT_TRUE(
        CurrentPlayer().WriteSamples(2, kVideoTrack, {&sample, 1}).ok());
    EXPECT_EQ(CurrentPlayer().WriteSamples(2, kVideoTrack, {&sample, 1}).code(),
              absl::StatusCode::kFailedPrecondition);
  });
}

TEST_P(SeekTicketConformance, FutureTicketIsRejectedWithoutConsumingTheGrant) {
  ALUMBRA_PORT_REQUIRE(Capability::kDataRequestHook);
  ASSERT_TRUE(Create().ok());
  ASSERT_TRUE(Grant(kVideoTrack));
  const auto sample = MakeSample(kVideoTrack, 0);
  Run([&] {
    EXPECT_EQ(CurrentPlayer()
                  .WriteSamples(Ticket() + 1, kVideoTrack, {&sample, 1})
                  .code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(CurrentPlayer().EndOfStream(Ticket() + 1, kVideoTrack).code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_TRUE(
        CurrentPlayer().WriteSamples(Ticket(), kVideoTrack, {&sample, 1}).ok());
  });
}

TEST_P(SeekTicketConformance, SeekInvalidatesOutstandingGrants) {
  ALUMBRA_PORT_REQUIRE(Capability::kDataRequestHook);
  ASSERT_TRUE(Create().ok());
  ASSERT_TRUE(Grant(kVideoTrack));
  ASSERT_TRUE(DoSeek(2, MediaTime::Zero()).ok());
  const auto sample = MakeSample(kVideoTrack, 0);
  Run([&] {
    EXPECT_EQ(CurrentPlayer().WriteSamples(2, kVideoTrack, {&sample, 1}).code(),
              absl::StatusCode::kFailedPrecondition);
  });
}

TEST_P(SeekTicketConformance, FlushInvalidatesGrantsAndEndOfStream) {
  ALUMBRA_PORT_REQUIRE(Capability::kDataRequestHook);
  ASSERT_TRUE(Create().ok());
  ASSERT_TRUE(Grant(kVideoTrack));
  Run([this] {
    EXPECT_TRUE(CurrentPlayer().EndOfStream(Ticket(), kVideoTrack).ok());
  });
  ASSERT_TRUE(DoFlush(2).ok());
  const auto sample = MakeSample(kVideoTrack, 0);
  Run([&] {
    EXPECT_EQ(CurrentPlayer().WriteSamples(2, kVideoTrack, {&sample, 1}).code(),
              absl::StatusCode::kFailedPrecondition);
  });
  ASSERT_TRUE(Grant(kVideoTrack));
  Run([&] {
    EXPECT_TRUE(
        CurrentPlayer().WriteSamples(2, kVideoTrack, {&sample, 1}).ok());
  });
}

TEST_P(SeekTicketConformance, SeekResetsEndOfStreamForEveryTrack) {
  ALUMBRA_PORT_REQUIRE(Capability::kDataRequestHook);
  ASSERT_TRUE(Create().ok());
  ASSERT_TRUE(Grant(kVideoTrack));
  Run([this] {
    EXPECT_TRUE(CurrentPlayer().EndOfStream(Ticket(), kVideoTrack).ok());
  });
  ASSERT_TRUE(DoSeek(2, MediaTime::Zero()).ok());
  ASSERT_TRUE(Grant(kVideoTrack));
  Run([this] {
    EXPECT_TRUE(CurrentPlayer().EndOfStream(2, kVideoTrack).ok());
  });
}

TEST_P(SeekTicketConformance, EventsQueuedBeforeASeekAreNeverDelivered) {
  ALUMBRA_PORT_REQUIRE(Capability::kDataRequestHook);
  ASSERT_TRUE(Create().ok());
  Run([this] {
    EXPECT_TRUE(Harness().RequestData(CurrentPlayer(), kVideoTrack, 1).ok());
    EXPECT_TRUE(Dispatcher().AdvanceTicket(2).ok());
    EXPECT_TRUE(CurrentPlayer().Seek(2, MediaTime::Zero()).ok());
  });
  SetTicket(2);
  EXPECT_TRUE(Receiver().Events().empty());
  ASSERT_TRUE(Grant(kVideoTrack));
  for (const auto& event : Receiver().Events()) {
    EXPECT_EQ(event.ticket, 2U);
  }
}

TEST_P(SeekTicketConformance, TicketsNearExhaustionStillAdvance) {
  ASSERT_TRUE(Create().ok());
  constexpr auto kLast = std::numeric_limits<SeekTicket>::max();
  EXPECT_TRUE(DoSeek(kLast, MediaTime::Zero()).ok());
  Run([this] {
    EXPECT_EQ(CurrentPlayer().Flush(kLast).code(),
              absl::StatusCode::kInvalidArgument);
  });
}

}
