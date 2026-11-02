#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port_types.h"
#include "penumbra_conformance/conformance.h"
#include "penumbra_conformance/port_harness.h"

namespace penumbra::conformance {
namespace {

constexpr absl::string_view kPayloadUrl = "type.alumbra.dev/conformance";

std::optional<PortError> LastError(const std::vector<PlayerEvent>& events) {
  std::optional<PortError> result;
  for (const auto& event : events) {
    if (const auto* error = std::get_if<PortError>(&event.detail)) {
      result = *error;
    }
  }
  return result;
}

}

TEST_P(ErrorReportingConformance, ErrorEventKeepsStatusCategoryAndHint) {
  ALUMBRA_PORT_REQUIRE(Capability::kErrorInjection);
  ASSERT_TRUE(Create().ok());
  Run([this] {
    EXPECT_TRUE(
        Harness()
            .InjectError(CurrentPlayer(),
                         {.status = absl::UnavailableError("device lost"),
                          .category = PortErrorCategory::kOutput,
                          .recovery = RecoveryHint::kRecreate})
            .ok());
  });
  const auto error = LastError(Receiver().Events());
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->status.code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(error->status.message(), "device lost");
  EXPECT_EQ(error->category, PortErrorCategory::kOutput);
  EXPECT_EQ(error->recovery, RecoveryHint::kRecreate);
}

TEST_P(ErrorReportingConformance, StatusPayloadsSurviveTheBoundary) {
  ALUMBRA_PORT_REQUIRE(Capability::kErrorInjection);
  ASSERT_TRUE(Create().ok());
  absl::Status status = absl::InternalError("decoder failed");
  status.SetPayload(kPayloadUrl, absl::Cord("detail"));
  Run([&] {
    EXPECT_TRUE(Harness()
                    .InjectError(CurrentPlayer(),
                                 {.status = status,
                                  .category = PortErrorCategory::kDecode,
                                  .recovery = RecoveryHint::kTransient})
                    .ok());
  });
  const auto error = LastError(Receiver().Events());
  ASSERT_TRUE(error.has_value());
  const auto payload = error->status.GetPayload(kPayloadUrl);
  ASSERT_TRUE(payload.has_value());
  EXPECT_EQ(std::string(*payload), "detail");
}

TEST_P(ErrorReportingConformance, AnOkStatusIsNeverReportedAsAnError) {
  ALUMBRA_PORT_REQUIRE(Capability::kErrorInjection);
  ASSERT_TRUE(Create().ok());
  Run([this] {
    EXPECT_FALSE(Harness().InjectError(CurrentPlayer(), {}).ok());
  });
  EXPECT_FALSE(LastError(Receiver().Events()).has_value());
}

TEST_P(ErrorReportingConformance, ErrorsCarryTheCurrentTicket) {
  ALUMBRA_PORT_REQUIRE(Capability::kErrorInjection);
  ASSERT_TRUE(Create().ok());
  ASSERT_TRUE(DoSeek(2, MediaTime::Zero()).ok());
  Run([this] {
    EXPECT_TRUE(Harness()
                    .InjectError(CurrentPlayer(),
                                 {.status = absl::DataLossError("corrupt"),
                                  .category = PortErrorCategory::kDecode,
                                  .recovery = RecoveryHint::kFatal})
                    .ok());
  });
  const auto events = Receiver().Events();
  ASSERT_FALSE(events.empty());
  EXPECT_EQ(events.back().ticket, 2U);
}

}
