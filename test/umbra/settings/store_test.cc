#include "umbra/settings/store.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/status.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/test_support/test_task_environment.h"
#include "umbra/settings/platform.h"
#include "umbra/settings/setting_types.h"
#include "umbra/settings/settings_keys.h"

namespace umbra::settings {
namespace {

using base::TaskPriority;
using base::TestTaskEnvironment;

class StoreTest : public ::testing::Test {
 protected:
  Store store_{GetRegistry(), Platform::kLinux};
};

TEST_F(StoreTest, DefaultsComeFromTheSchema) {
  EXPECT_EQ(store_.Get(kNetworkRetryMaxAttempts), 3);
  EXPECT_EQ(store_.Get(kNetworkRetryInitialBackoffMs), absl::Milliseconds(500));
  EXPECT_EQ(store_.Get(kObservabilityLogLevel), ObservabilityLogLevel::kInfo);
  EXPECT_TRUE(store_.Get(kObservabilityLogCategories).empty());
  EXPECT_EQ(store_.Get(kPlayerEventsTimeupdateIntervalMs),
            absl::Milliseconds(250));
  EXPECT_EQ(store_.Get(kEngineThreadsMaxWorkers), 0);
}

TEST_F(StoreTest, EachLayerOverridesLowerOnesAndResetRestoresTheLowerValue) {
  const Key<std::int64_t> key = kNetworkRetryMaxAttempts;
  ASSERT_TRUE(store_.Set(Layer::kPortDefault, key, 5).ok());
  EXPECT_EQ(store_.Get(key), 5);
  ASSERT_TRUE(store_.Set(Layer::kIntegrator, key, 6).ok());
  EXPECT_EQ(store_.Get(key), 6);
  ASSERT_TRUE(store_.Set(Layer::kUser, key, 7).ok());
  EXPECT_EQ(store_.Get(key), 7);
  ASSERT_TRUE(store_.Set(Layer::kSource, key, 8).ok());
  EXPECT_EQ(store_.Get(key), 8);

  store_.Reset(Layer::kSource, key);
  EXPECT_EQ(store_.Get(key), 7);
  store_.Reset(Layer::kUser, key);
  EXPECT_EQ(store_.Get(key), 6);
  store_.Reset(Layer::kIntegrator, key);
  EXPECT_EQ(store_.Get(key), 5);
  store_.Reset(Layer::kPortDefault, key);
  EXPECT_EQ(store_.Get(key), 3);
}

TEST_F(StoreTest, HigherLayersWinRegardlessOfWriteOrder) {
  const Key<std::int64_t> key = kNetworkRetryMaxAttempts;
  ASSERT_TRUE(store_.Set(Layer::kSource, key, 8).ok());
  ASSERT_TRUE(store_.Set(Layer::kPortDefault, key, 5).ok());
  ASSERT_TRUE(store_.Set(Layer::kUser, key, 7).ok());
  EXPECT_EQ(store_.Get(key), 8);
}

TEST_F(StoreTest, ResetLayerClearsEveryKeyInThatLayerOnly) {
  ASSERT_TRUE(store_.Set(Layer::kUser, kNetworkRetryMaxAttempts, 9).ok());
  ASSERT_TRUE(store_
                  .Set(Layer::kUser, kObservabilityLogLevel,
                       ObservabilityLogLevel::kError)
                  .ok());
  ASSERT_TRUE(store_.Set(Layer::kIntegrator, kNetworkRetryMaxAttempts, 4).ok());
  store_.Reset(Layer::kUser);
  EXPECT_EQ(store_.Get(kNetworkRetryMaxAttempts), 4);
  EXPECT_EQ(store_.Get(kObservabilityLogLevel), ObservabilityLogLevel::kInfo);
}

TEST_F(StoreTest, InvalidSetReturnsInvalidArgumentAndLeavesTheStoreUnchanged) {
  ASSERT_TRUE(store_.Set(Layer::kUser, kNetworkRetryMaxAttempts, 5).ok());
  for (const std::int64_t invalid :
       {std::int64_t{-1}, std::int64_t{2147483648}}) {
    const absl::Status status =
        store_.Set(Layer::kUser, kNetworkRetryMaxAttempts, invalid);
    EXPECT_TRUE(absl::IsInvalidArgument(status)) << status;
    EXPECT_EQ(store_.Get(kNetworkRetryMaxAttempts), 5);
    const auto detail = base::GetErrorDetail(status);
    ASSERT_TRUE(detail.ok());
    ASSERT_TRUE(detail->has_value());
    EXPECT_EQ((*detail)->code, base::ErrorCode::kSettingsInvalid);
    EXPECT_EQ((*detail)->context.at("setting"), "network.retry.max_attempts");
  }
  EXPECT_TRUE(absl::IsInvalidArgument(store_.Set(
      Layer::kUser, kEngineThreadsYieldBudgetMs, absl::Milliseconds(9))));
}

TEST_F(StoreTest, DurationsMustBeWholeMilliseconds) {
  EXPECT_TRUE(absl::IsInvalidArgument(
      store_.Set(Layer::kUser, kPlayerEventsTimeupdateIntervalMs,
                 absl::Microseconds(1500))));
  EXPECT_TRUE(absl::IsInvalidArgument(
      store_.Set(Layer::kUser, kPlayerEventsTimeupdateIntervalMs,
                 absl::InfiniteDuration())));
  EXPECT_EQ(store_.Get(kPlayerEventsTimeupdateIntervalMs),
            absl::Milliseconds(250));
  ASSERT_TRUE(store_
                  .Set(Layer::kUser, kPlayerEventsTimeupdateIntervalMs,
                       absl::Milliseconds(40))
                  .ok());
  EXPECT_EQ(store_.Get(kPlayerEventsTimeupdateIntervalMs),
            absl::Milliseconds(40));
}

TEST_F(StoreTest, StringKeysAndAssignmentsUseTheSameValidation) {
  ASSERT_TRUE(
      store_.SetFromAssignment(Layer::kUser, "network.retry.max_attempts=9")
          .ok());
  EXPECT_EQ(store_.Get(kNetworkRetryMaxAttempts), 9);
  ASSERT_TRUE(store_
                  .SetFromString(Layer::kUser, "observability.log.categories",
                                 "net,abr")
                  .ok());
  EXPECT_EQ(store_.Get(kObservabilityLogCategories),
            (std::vector<std::string>{"net", "abr"}));
  for (const char* invalid :
       {"network.retry.max_attempts=-3", "network.retry.max_attempts=abc",
        "network.retry.max_attempts", "=5", "unknown.key=1",
        "observability.log.level=loud", "observability.log.categories=net,,abr",
        "observability.log.categories=unknown"}) {
    EXPECT_TRUE(absl::IsInvalidArgument(
        store_.SetFromAssignment(Layer::kUser, invalid)))
        << invalid;
  }
  EXPECT_EQ(store_.Get(kNetworkRetryMaxAttempts), 9);
  EXPECT_EQ(store_.Get(kObservabilityLogLevel), ObservabilityLogLevel::kInfo);
}

TEST_F(StoreTest, IntegratorLockExcludesTheUserLayerIncludingOlderValues) {
  const Key<std::int64_t> key = kNetworkRetryMaxAttempts;
  ASSERT_TRUE(store_.Set(Layer::kUser, key, 7).ok());
  ASSERT_TRUE(store_.Set(Layer::kIntegrator, key, 6).ok());
  store_.SetUserLocked(key, true);
  EXPECT_EQ(store_.Get(key), 6);
  EXPECT_TRUE(absl::IsInvalidArgument(store_.Set(Layer::kUser, key, 8)));
  EXPECT_EQ(store_.Get(key), 6);
  ASSERT_TRUE(store_.Set(Layer::kSource, key, 9).ok());
  EXPECT_EQ(store_.Get(key), 9);
  store_.SetUserLocked(key, false);
  store_.Reset(Layer::kSource, key);
  EXPECT_EQ(store_.Get(key), 7);
}

TEST_F(StoreTest, RestartKeysCannotBeOverriddenPerSource) {
  EXPECT_TRUE(absl::IsInvalidArgument(
      store_.Set(Layer::kSource, kEngineThreadsMaxWorkers, 4)));
  EXPECT_TRUE(store_.Set(Layer::kUser, kEngineThreadsMaxWorkers, 4).ok());
  EXPECT_TRUE(store_
                  .Set(Layer::kSource, kObservabilityLogLevel,
                       ObservabilityLogLevel::kDebug)
                  .ok());
}

TEST_F(StoreTest, UnsupportedPlatformWritesAreAcceptedAndIgnored) {
  EXPECT_FALSE(store_.IsSupported(kEngineThreadsYieldBudgetMs));
  EXPECT_TRUE(store_.IsSupported(kNetworkRetryMaxAttempts));
  EXPECT_TRUE(
      store_
          .Set(Layer::kUser, kEngineThreadsYieldBudgetMs, absl::Milliseconds(4))
          .ok());
  EXPECT_EQ(store_.Get(kEngineThreadsYieldBudgetMs), absl::ZeroDuration());

  Store web(GetRegistry(), Platform::kWeb);
  EXPECT_TRUE(web.IsSupported(kEngineThreadsYieldBudgetMs));
  ASSERT_TRUE(
      web.Set(Layer::kUser, kEngineThreadsYieldBudgetMs, absl::Milliseconds(4))
          .ok());
  EXPECT_EQ(web.Get(kEngineThreadsYieldBudgetMs), absl::Milliseconds(4));
}

TEST_F(StoreTest, AutomaticZeroIsALiteralValueWithAnExplicitPolicy) {
  const SettingInfo& info =
      store_.Schema().settings[kEngineThreadsMaxWorkers.Index()];
  EXPECT_EQ(info.auto_policy, "hardware_concurrency_budget");
  EXPECT_EQ(
      store_.Schema().settings[kNetworkRetryMaxAttempts.Index()].auto_policy,
      "");
}

TEST_F(StoreTest, SnapshotsAreImmutableViews) {
  ASSERT_TRUE(store_.Set(Layer::kUser, kEngineThreadsMaxWorkers, 2).ok());
  const Snapshot before = store_.TakeSnapshot();
  ASSERT_TRUE(store_.Set(Layer::kUser, kEngineThreadsMaxWorkers, 6).ok());
  const Snapshot after = store_.TakeSnapshot();
  EXPECT_EQ(before.Get(kEngineThreadsMaxWorkers), 2);
  EXPECT_EQ(after.Get(kEngineThreadsMaxWorkers), 6);
  EXPECT_EQ(store_.Get(kEngineThreadsMaxWorkers), 6);
}

class ObserverTest : public StoreTest {
 protected:
  TestTaskEnvironment environment_;
  std::shared_ptr<base::SequencedTaskRunner> runner_ =
      environment_.CreateSequencedTaskRunner(TaskPriority::kControl);
  std::vector<ObservabilityLogLevel> seen_;

  absl::StatusOr<Subscription> Watch(
      const std::shared_ptr<base::SequencedTaskRunner>& runner) {
    return store_.Observe<ObservabilityLogLevel>(
        kObservabilityLogLevel, runner,
        [this, runner](const ObservabilityLogLevel& level) {
          EXPECT_TRUE(runner->RunsTasksInCurrentSequence());
          seen_.push_back(level);
        });
  }

  void SetLevel(Layer layer, ObservabilityLogLevel level) {
    ASSERT_TRUE(store_.Set(layer, kObservabilityLogLevel, level).ok());
  }
};

TEST_F(ObserverTest, NotificationsAreAsynchronousOnTheSubscribersRunner) {
  auto subscription = Watch(runner_);
  ASSERT_TRUE(subscription.ok());
  SetLevel(Layer::kUser, ObservabilityLogLevel::kDebug);
  EXPECT_TRUE(seen_.empty());
  environment_.RunUntilIdle();
  EXPECT_EQ(
      seen_,
      (std::vector<ObservabilityLogLevel>{ObservabilityLogLevel::kDebug}));
}

TEST_F(ObserverTest, FiresOncePerEffectiveChangeAndNeverForNoOpWrites) {
  auto subscription = Watch(runner_);
  ASSERT_TRUE(subscription.ok());
  SetLevel(Layer::kUser, ObservabilityLogLevel::kInfo);
  SetLevel(Layer::kUser, ObservabilityLogLevel::kDebug);
  SetLevel(Layer::kUser, ObservabilityLogLevel::kDebug);
  SetLevel(Layer::kIntegrator, ObservabilityLogLevel::kDebug);
  SetLevel(Layer::kPortDefault, ObservabilityLogLevel::kError);
  store_.Reset(Layer::kSource);
  EXPECT_FALSE(store_
                   .Set(Layer::kUser, kObservabilityLogLevel,
                        static_cast<ObservabilityLogLevel>(99))
                   .ok());
  environment_.RunUntilIdle();
  EXPECT_EQ(
      seen_,
      (std::vector<ObservabilityLogLevel>{ObservabilityLogLevel::kDebug}));

  store_.Reset(Layer::kUser);
  environment_.RunUntilIdle();
  EXPECT_EQ(seen_.size(), 1);
  store_.Reset(Layer::kIntegrator);
  environment_.RunUntilIdle();
  EXPECT_EQ(seen_,
            (std::vector<ObservabilityLogLevel>{
                ObservabilityLogLevel::kDebug, ObservabilityLogLevel::kError}));
}

TEST_F(ObserverTest, LockChangesNotifyWhenTheEffectiveValueChanges) {
  auto subscription = Watch(runner_);
  ASSERT_TRUE(subscription.ok());
  SetLevel(Layer::kUser, ObservabilityLogLevel::kDebug);
  environment_.RunUntilIdle();
  store_.SetUserLocked(kObservabilityLogLevel, true);
  store_.SetUserLocked(kObservabilityLogLevel, true);
  environment_.RunUntilIdle();
  store_.SetUserLocked(kObservabilityLogLevel, false);
  environment_.RunUntilIdle();
  EXPECT_EQ(seen_,
            (std::vector<ObservabilityLogLevel>{
                ObservabilityLogLevel::kDebug, ObservabilityLogLevel::kInfo,
                ObservabilityLogLevel::kDebug}));
}

TEST_F(ObserverTest, EachSubscriberIsDeliveredOnItsOwnRunnerAndCanCancel) {
  auto other_runner =
      environment_.CreateSequencedTaskRunner(TaskPriority::kBackground);
  auto first = Watch(runner_);
  ASSERT_TRUE(first.ok());
  std::vector<ObservabilityLogLevel> second_seen;
  auto second = store_.Observe<ObservabilityLogLevel>(
      kObservabilityLogLevel, other_runner,
      [&](const ObservabilityLogLevel& level) {
        EXPECT_TRUE(other_runner->RunsTasksInCurrentSequence());
        second_seen.push_back(level);
      });
  ASSERT_TRUE(second.ok());
  SetLevel(Layer::kUser, ObservabilityLogLevel::kWarning);
  first->Cancel();
  environment_.RunUntilIdle();
  EXPECT_TRUE(seen_.empty());
  EXPECT_EQ(
      second_seen,
      (std::vector<ObservabilityLogLevel>{ObservabilityLogLevel::kWarning}));
  SetLevel(Layer::kUser, ObservabilityLogLevel::kError);
  environment_.RunUntilIdle();
  EXPECT_TRUE(seen_.empty());
  EXPECT_EQ(second_seen.size(), 2);
}

TEST_F(ObserverTest, DestroyingTheSubscriptionStopsDelivery) {
  {
    auto subscription = Watch(runner_);
    ASSERT_TRUE(subscription.ok());
  }
  SetLevel(Layer::kUser, ObservabilityLogLevel::kDebug);
  environment_.RunUntilIdle();
  EXPECT_TRUE(seen_.empty());
}

TEST_F(ObserverTest, OnlyLiveKeysCanBeObserved) {
  const auto result = store_.Observe<std::int64_t>(
      kNetworkRetryMaxAttempts, runner_, [](const std::int64_t&) {});
  EXPECT_TRUE(absl::IsFailedPrecondition(result.status()));
}

TEST_F(ObserverTest, NotificationsOutliveTheStoreSafely) {
  std::vector<std::vector<std::string>> lists;
  absl::StatusOr<Subscription> subscription;
  {
    Store store(GetRegistry(), Platform::kLinux);
    subscription = store.Observe<std::vector<std::string>>(
        kObservabilityLogCategories, runner_,
        [&](const std::vector<std::string>& value) { lists.push_back(value); });
    ASSERT_TRUE(subscription.ok());
    ASSERT_TRUE(
        store.Set(Layer::kUser, kObservabilityLogCategories, {"net"}).ok());
  }
  environment_.RunUntilIdle();
  EXPECT_EQ(lists, (std::vector<std::vector<std::string>>{{"net"}}));
}

}
}
