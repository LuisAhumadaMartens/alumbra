#include <cstddef>
#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"
#include "penumbra/interface/interface.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/stream_info.h"
#include "penumbra_conformance/conformance.h"
#include "penumbra_conformance/port_harness.h"

namespace penumbra::conformance {
namespace {

constexpr size_t kMaxExhaustiblePlayers = 8;

}

TEST_P(LifecycleConformance, LimitsReportTheCurrentApiVersion) {
  absl::StatusOr<PortLimits> limits;
  Run([&] { limits = Harness().UnderTest().GetLimits(); });
  ASSERT_TRUE(limits.ok()) << limits.status();
  EXPECT_EQ(limits->api_version, kApiVersion);
  EXPECT_GE(limits->max_players, 1U);
}

TEST_P(LifecycleConformance, CreatesAndDestroysAPlayerWithoutWork) {
  ASSERT_TRUE(Create().ok());
  DestroyPlayer();
}

TEST_P(LifecycleConformance, StopIsTerminalAndIdempotent) {
  ASSERT_TRUE(Create().ok());
  Run([this] {
    EXPECT_TRUE(CurrentPlayer().Stop().ok());
    EXPECT_TRUE(CurrentPlayer().Stop().ok());
    EXPECT_FALSE(CurrentPlayer().Seek(Ticket() + 1, MediaTime::Zero()).ok());
    EXPECT_FALSE(CurrentPlayer().GetSnapshot().ok());
  });
}

TEST_P(LifecycleConformance, DestroyWithPendingWorkDeliversNothingAfterStop) {
  ASSERT_TRUE(Create().ok());
  Run([this] {
    if (Has(Capability::kDataRequestHook)) {
      EXPECT_TRUE(Harness().RequestData(CurrentPlayer(), kVideoTrack, 1).ok());
    }
    EXPECT_TRUE(CurrentPlayer().Stop().ok());
    Dispatcher().Stop();
  });
  DestroyPlayer();
  Settle();
  EXPECT_TRUE(Receiver().Events().empty());
  EXPECT_TRUE(Dispatcher().GetDeliveryStatus().ok());
}

TEST_P(LifecycleConformance, CreationFailsAtTheInstanceLimit) {
  absl::StatusOr<PortLimits> limits;
  Run([&] { limits = Harness().UnderTest().GetLimits(); });
  ASSERT_TRUE(limits.ok()) << limits.status();
  if (limits->max_players > kMaxExhaustiblePlayers) {
    GTEST_SKIP() << "limit of " << limits->max_players
                 << " players is too high to exhaust";
  }
  ASSERT_TRUE(Create().ok());
  for (size_t i = 1; i < limits->max_players; ++i) {
    ASSERT_TRUE(CreateExtra().ok());
  }
  auto receiver = std::make_shared<RecordingReceiver>(Control());
  const std::weak_ptr<RecordingReceiver> retained = receiver;
  const auto status = TryCreate(DefaultPlayerConfig(), std::move(receiver));
  EXPECT_EQ(status.code(), absl::StatusCode::kResourceExhausted);
  EXPECT_TRUE(retained.expired());
  DestroyPlayer();
  EXPECT_TRUE(Create().ok());
}

TEST_P(LifecycleConformance, RejectsInvalidConfigurationWithoutRetainingIt) {
  auto receiver = std::make_shared<RecordingReceiver>(Control());
  const std::weak_ptr<RecordingReceiver> retained = receiver;

  auto wrong_version = DefaultPlayerConfig();
  wrong_version.api_version = kApiVersion + 1;
  EXPECT_EQ(TryCreate(std::move(wrong_version), receiver).code(),
            absl::StatusCode::kInvalidArgument);

  auto zero_ticket = DefaultPlayerConfig();
  zero_ticket.initial_ticket = 0;
  EXPECT_EQ(TryCreate(std::move(zero_ticket), receiver).code(),
            absl::StatusCode::kInvalidArgument);

  auto duplicate = DefaultPlayerConfig();
  duplicate.streams = {VideoStream(kVideoTrack), VideoStream(kVideoTrack)};
  EXPECT_EQ(TryCreate(std::move(duplicate), receiver).code(),
            absl::StatusCode::kInvalidArgument);

  EXPECT_EQ(TryCreate(DefaultPlayerConfig(), nullptr).code(),
            absl::StatusCode::kInvalidArgument);
  receiver.reset();
  EXPECT_TRUE(retained.expired());
}

TEST_P(LifecycleConformance, UnsupportedCodecIsReportedAsUnimplemented) {
  auto unknown = VideoStream();
  unknown.info.codec = CodecId::kUnknown;
  unknown.info.codec_string = "unsupported.codec";
  absl::StatusOr<DecodingCapability> capability;
  Run([&] {
    capability = Harness().UnderTest().QueryDecoding({.stream = unknown.info});
  });
  ASSERT_TRUE(capability.ok()) << capability.status();
  if (capability->supported) {
    GTEST_SKIP() << "port claims support for the unknown codec";
  }
  auto config = DefaultPlayerConfig();
  config.streams = {unknown};
  EXPECT_EQ(TryCreate(std::move(config),
                      std::make_shared<RecordingReceiver>(Control()))
                .code(),
            absl::StatusCode::kUnimplemented);
}

}
