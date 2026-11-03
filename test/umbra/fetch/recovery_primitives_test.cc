#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "penumbra/interface/http_transport.h"
#include "recovery_test_support.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/status.h"
#include "umbra/base/time_ticks.h"
#include "umbra/base/url_redactor.h"
#include "umbra/fetch/backoff.h"
#include "umbra/fetch/error_classifier.h"
#include "umbra/fetch/fetch_failure.h"
#include "umbra/fetch/pathway_table.h"
#include "umbra/fetch/recovery_errors.h"
#include "umbra/fetch/recovery_ladder.h"

namespace umbra::fetch::test {
namespace {

using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::Ge;
using ::testing::Lt;

base::TimeTicks At(int64_t ms) {
  return base::TimeTicks::FromSinceOrigin(absl::Milliseconds(ms));
}

FetchFailure Failure(int http_status,
                     std::optional<absl::Duration> retry_after = {}) {
  FetchFailure failure;
  failure.url = "https://a.test/seg0.m4s";
  failure.http_status = http_status;
  failure.retry_after = retry_after;
  return failure;
}

TEST(BackoffTest, CeilingDoublesFromInitialUpToMaximum) {
  const BackoffPolicy policy;
  std::vector<int64_t> ceilings;
  ceilings.reserve(7);
  for (int attempt = 0; attempt < 7; ++attempt) {
    ceilings.push_back(
        absl::ToInt64Milliseconds(BackoffCeiling(policy, attempt)));
  }
  EXPECT_THAT(ceilings, ElementsAre(500, 1000, 2000, 4000, 8000, 8000, 8000));
  EXPECT_EQ(absl::ToInt64Milliseconds(BackoffCeiling(policy, 1000)), 8000);
}

TEST(BackoffTest, DelayIsEqualJitterBetweenHalfAndFullCeiling) {
  const BackoffPolicy policy;
  FixedJitter low(0.0);
  FixedJitter high(1.0);
  EXPECT_EQ(absl::ToInt64Milliseconds(BackoffDelay(policy, 2, low)), 1000);
  EXPECT_EQ(absl::ToInt64Milliseconds(BackoffDelay(policy, 2, high)), 2000);
}

TEST(BackoffTest, SeededJitterIsDeterministicAndInRange) {
  SeededJitter first(42);
  SeededJitter second(42);
  SeededJitter other(43);
  std::vector<double> a;
  std::vector<double> b;
  std::vector<double> c;
  for (int i = 0; i < 64; ++i) {
    a.push_back(first.NextUnit());
    b.push_back(second.NextUnit());
    c.push_back(other.NextUnit());
  }
  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);
  EXPECT_THAT(a, Each(Ge(0.0)));
  EXPECT_THAT(a, Each(Lt(1.0)));
}

TEST(BackoffTest, SeededDelaysStayInsideTheJitterBand) {
  const BackoffPolicy policy;
  SeededJitter jitter(7);
  for (int attempt = 0; attempt < 6; ++attempt) {
    const absl::Duration ceiling = BackoffCeiling(policy, attempt);
    const absl::Duration delay = BackoffDelay(policy, attempt, jitter);
    EXPECT_GE(delay, ceiling / 2);
    EXPECT_LE(delay, ceiling);
  }
}

TEST(BackoffTest, ValidationRejectsInconsistentPolicies) {
  EXPECT_TRUE(ValidateBackoffPolicy(BackoffPolicy{}).ok());
  BackoffPolicy policy;
  policy.max_attempts = -1;
  EXPECT_FALSE(ValidateBackoffPolicy(policy).ok());
  policy = BackoffPolicy{};
  policy.max_backoff = absl::Milliseconds(100);
  EXPECT_FALSE(ValidateBackoffPolicy(policy).ok());
  policy = BackoffPolicy{};
  policy.initial_backoff = absl::Milliseconds(-1);
  EXPECT_FALSE(ValidateBackoffPolicy(policy).ok());
}

TEST(FetchFailureTest, RetryAfterAcceptsOnlyDeltaSeconds) {
  EXPECT_EQ(ParseRetryAfter({{.name = "Retry-After", .value = "3"}}),
            absl::Seconds(3));
  EXPECT_EQ(ParseRetryAfter({{.name = "retry-after", .value = " 0 "}}),
            absl::ZeroDuration());
  EXPECT_FALSE(ParseRetryAfter({}).has_value());
  EXPECT_FALSE(ParseRetryAfter({{.name = "Retry-After", .value = ""}}));
  EXPECT_FALSE(ParseRetryAfter({{.name = "Retry-After", .value = "soon"}}));
  EXPECT_FALSE(ParseRetryAfter({{.name = "Retry-After", .value = "-5"}}));
  EXPECT_FALSE(ParseRetryAfter(
      {{.name = "Retry-After", .value = "Wed, 21 Oct 2026 07:28:00 GMT"}}));
  EXPECT_FALSE(ParseRetryAfter({{.name = "Retry-After", .value = "9999999"}}));
}

TEST(FetchFailureTest, PayloadRoundTripsThroughStatus) {
  FetchFailure failure;
  failure.url = "https://a.test/path|with|bars?x=1";
  failure.request_class = penumbra::RequestClass::kInit;
  failure.http_status = 503;
  failure.retry_after = absl::Seconds(2);
  const absl::Status status =
      AttachFetchFailure(absl::UnavailableError("x"), failure);
  const std::optional<FetchFailure> read = GetFetchFailure(status);
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ(read->url, failure.url);
  EXPECT_EQ(read->request_class, penumbra::RequestClass::kInit);
  EXPECT_EQ(read->http_status, 503);
  EXPECT_EQ(read->retry_after, absl::Seconds(2));
  EXPECT_FALSE(GetFetchFailure(absl::UnavailableError("x")).has_value());
  EXPECT_TRUE(AttachFetchFailure(absl::OkStatus(), failure).ok());
}

TEST(ClassifierTest, TransientFailuresRetry) {
  const BackoffPolicy policy;
  for (const absl::StatusCode code :
       {absl::StatusCode::kUnavailable, absl::StatusCode::kDeadlineExceeded,
        absl::StatusCode::kDataLoss}) {
    const Classification result = ClassifyFailure(
        absl::Status(code, "x"), Failure(503), LivePosition::kNotLive, policy);
    EXPECT_EQ(result.action, RecoveryAction::kRetry);
    EXPECT_FALSE(result.retry_delay.has_value());
  }
}

TEST(ClassifierTest, RetryAfterWithinMaximumSetsTheDelay) {
  const Classification result = ClassifyFailure(
      absl::UnavailableError("x"), Failure(429, absl::Seconds(2)),
      LivePosition::kNotLive, BackoffPolicy{});
  EXPECT_EQ(result.action, RecoveryAction::kRetry);
  EXPECT_EQ(result.retry_delay, absl::Seconds(2));
}

TEST(ClassifierTest, RetryAfterBeyondMaximumFailsOverAtOnce) {
  const Classification result = ClassifyFailure(
      absl::UnavailableError("x"), Failure(503, absl::Seconds(120)),
      LivePosition::kNotLive, BackoffPolicy{});
  EXPECT_EQ(result.action, RecoveryAction::kFailover);
  EXPECT_EQ(result.pathway_hold, absl::Seconds(120));
  EXPECT_TRUE(result.pathway_wide);
}

TEST(ClassifierTest, TooManyRequestsWithoutRetryAfterUsesOrdinaryBackoff) {
  const Classification result =
      ClassifyFailure(absl::UnavailableError("x"), Failure(429),
                      LivePosition::kNotLive, BackoffPolicy{});
  EXPECT_EQ(result.action, RecoveryAction::kRetry);
  EXPECT_FALSE(result.retry_delay.has_value());
  EXPECT_FALSE(result.pathway_hold.has_value());
}

TEST(ClassifierTest, MissingSegmentDependsOnLivePosition) {
  const BackoffPolicy policy;
  const absl::Status missing = absl::NotFoundError("x");
  EXPECT_EQ(
      ClassifyFailure(missing, Failure(404), LivePosition::kNotLive, policy)
          .action,
      RecoveryAction::kFailover);
  EXPECT_FALSE(
      ClassifyFailure(missing, Failure(404), LivePosition::kNotLive, policy)
          .pathway_wide);
  EXPECT_EQ(
      ClassifyFailure(missing, Failure(404), LivePosition::kNearEdge, policy)
          .action,
      RecoveryAction::kRetry);
  EXPECT_EQ(ClassifyFailure(missing, Failure(410), LivePosition::kBehindWindow,
                            policy)
                .action,
            RecoveryAction::kResync);
}

TEST(ClassifierTest, ForbiddenNeverRetriesAndSkipsRenditionExclusion) {
  const Classification result =
      ClassifyFailure(absl::PermissionDeniedError("x"), Failure(403),
                      LivePosition::kNotLive, BackoffPolicy{});
  EXPECT_EQ(result.action, RecoveryAction::kFailover);
  EXPECT_FALSE(result.allows_rendition_step);
}

TEST(ClassifierTest, OtherClientErrorsAreNotRetriedAndInternalErrorsAreFatal) {
  const BackoffPolicy policy;
  EXPECT_EQ(ClassifyFailure(absl::InvalidArgumentError("x"), Failure(400),
                            LivePosition::kNotLive, policy)
                .action,
            RecoveryAction::kFailover);
  EXPECT_EQ(ClassifyFailure(absl::FailedPreconditionError("x"), std::nullopt,
                            LivePosition::kNotLive, policy)
                .action,
            RecoveryAction::kFatal);
}

TEST(PathwayTableTest, NormalExclusionExpiresWithTime) {
  PathwayTable table;
  table.Exclude("cdn-a", At(0), absl::Seconds(300), false);
  EXPECT_TRUE(table.IsExcluded("cdn-a", At(299'999)));
  EXPECT_FALSE(table.IsExcluded("cdn-a", At(300'000)));
  EXPECT_FALSE(table.IsExcluded("cdn-b", At(0)));
}

TEST(PathwayTableTest, SteeringRefreshEndsNormalExclusionEarly) {
  PathwayTable table;
  table.Exclude("cdn-a", At(0), absl::Seconds(300), true);
  EXPECT_TRUE(table.IsExcluded("cdn-a", At(10'000)));
  table.OnSteeringRefresh();
  EXPECT_FALSE(table.IsExcluded("cdn-a", At(10'000)));
}

TEST(PathwayTableTest, RetryAfterDeadlineSurvivesSteeringRefresh) {
  PathwayTable table;
  table.Exclude("cdn-a", At(0), absl::Seconds(300), true);
  table.HoldUntil("cdn-a", At(120'000));
  table.OnSteeringRefresh();
  EXPECT_TRUE(table.IsExcluded("cdn-a", At(119'999)));
  EXPECT_FALSE(table.IsExcluded("cdn-a", At(120'000)));
}

TEST(PathwayTableTest, LongerExclusionWinsOverRetryAfterWithoutSteering) {
  PathwayTable table;
  table.Exclude("cdn-a", At(0), absl::Seconds(300), false);
  table.HoldUntil("cdn-a", At(120'000));
  EXPECT_TRUE(table.IsExcluded("cdn-a", At(200'000)));
  EXPECT_FALSE(table.IsExcluded("cdn-a", At(300'000)));
  table.Clear("cdn-a");
  EXPECT_FALSE(table.IsExcluded("cdn-a", At(0)));
}

TEST(RecoveryErrorsTest, ManifestFailureCarriesUrlAndStatus) {
  const base::UrlRedactor redactor;
  const absl::Status error = MakeManifestLoadError(
      redactor, "https://a.test/main.m3u8", absl::NotFoundError("x"), 404, 0);
  auto detail = base::GetErrorDetail(error);
  ASSERT_TRUE(detail.ok());
  ASSERT_TRUE(detail->has_value());
  EXPECT_EQ((*detail)->code, base::ErrorCode::kNetworkRequestFailed);
  EXPECT_EQ((*detail)->severity, base::ErrorSeverity::kFatal);
  EXPECT_EQ((*detail)->context.at("http_status"), "404");
  EXPECT_NE((*detail)->context.at("url").find("a.test"), std::string::npos);
}

TEST(RecoveryErrorsTest, ManifestClientErrorsAreFatalAtOnce) {
  ManifestLoadRecovery recovery(BackoffPolicy{},
                                std::make_shared<FixedJitter>(1.0),
                                std::make_shared<base::UrlRedactor>());
  const ManifestRetryDecision decision = recovery.OnFailure(
      "https://a.test/main.m3u8", absl::NotFoundError("x"), 404, {});
  EXPECT_FALSE(decision.retry);
  EXPECT_FALSE(decision.error.ok());
}

TEST(RecoveryErrorsTest, ManifestServerErrorsRetryThenFail) {
  ManifestLoadRecovery recovery(BackoffPolicy{},
                                std::make_shared<FixedJitter>(1.0),
                                std::make_shared<base::UrlRedactor>());
  std::vector<int64_t> delays;
  ManifestRetryDecision decision;
  for (int i = 0; i < 3; ++i) {
    decision = recovery.OnFailure("https://a.test/main.m3u8",
                                  absl::UnavailableError("x"), 503, {});
    EXPECT_TRUE(decision.retry);
    delays.push_back(absl::ToInt64Milliseconds(decision.delay));
  }
  EXPECT_THAT(delays, ElementsAre(500, 1000, 2000));
  decision = recovery.OnFailure("https://a.test/main.m3u8",
                                absl::UnavailableError("x"), 503, {});
  EXPECT_FALSE(decision.retry);
  auto detail = base::GetErrorDetail(decision.error);
  ASSERT_TRUE(detail.ok());
  ASSERT_TRUE(detail->has_value());
  EXPECT_EQ((*detail)->context.at("attempts"), "3");
  recovery.Reset();
  EXPECT_TRUE(recovery
                  .OnFailure("https://a.test/main.m3u8",
                             absl::UnavailableError("x"), 503, {})
                  .retry);
}

TEST(RecoveryConfigTest, ValidationRejectsNegativeExclusions) {
  EXPECT_TRUE(ValidateRecoveryConfig(RecoveryConfig{}).ok());
  RecoveryConfig config;
  config.rendition_exclusion = absl::Seconds(-1);
  EXPECT_FALSE(ValidateRecoveryConfig(config).ok());
  config = RecoveryConfig{};
  config.pathway_exclusion = absl::Seconds(-1);
  EXPECT_FALSE(ValidateRecoveryConfig(config).ok());
}

}
}
