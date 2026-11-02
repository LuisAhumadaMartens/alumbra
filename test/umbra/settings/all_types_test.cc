#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "umbra/settings/platform.h"
#include "umbra/settings/store.h"
#include "umbra/settings/testing/all_types_keys.h"

namespace umbra::settings {
namespace {

using all_types::GetRegistry;
using all_types::TestMode;

class AllTypesTest : public ::testing::Test {
 protected:
  Store store_{GetRegistry(), Platform::kLinux};
  Store web_store_{GetRegistry(), Platform::kWeb};
};

TEST_F(AllTypesTest, DefaultsOfEveryType) {
  EXPECT_TRUE(store_.Get(all_types::kTestEnabled));
  EXPECT_EQ(store_.Get(all_types::kTestCount), 2);
  EXPECT_EQ(store_.Get(all_types::kTestRatioPercent), 50.0);
  EXPECT_EQ(store_.Get(all_types::kTestMode), TestMode::kAuto);
  EXPECT_EQ(store_.Get(all_types::kTestLabel), "");
  EXPECT_EQ(store_.Get(all_types::kTestTags),
            (std::vector<std::string>{"first"}));
  EXPECT_EQ(store_.Get(all_types::kTestTimeoutMs), absl::Milliseconds(100));
  EXPECT_EQ(store_.Get(all_types::kTestCapacityBytes), 1024U);
}

TEST_F(AllTypesTest, TypedSetAndGetRoundTripEveryType) {
  ASSERT_TRUE(store_.Set(Layer::kUser, all_types::kTestEnabled, false).ok());
  ASSERT_TRUE(store_.Set(Layer::kUser, all_types::kTestCount, -5).ok());
  ASSERT_TRUE(
      web_store_.Set(Layer::kUser, all_types::kTestRatioPercent, 99.5).ok());
  ASSERT_TRUE(
      store_.Set(Layer::kUser, all_types::kTestMode, TestMode::kManual).ok());
  ASSERT_TRUE(store_.Set(Layer::kUser, all_types::kTestLabel, "hello").ok());
  ASSERT_TRUE(store_.Set(Layer::kUser, all_types::kTestTags, {"a", "b"}).ok());
  ASSERT_TRUE(store_
                  .Set(Layer::kUser, all_types::kTestTimeoutMs,
                       absl::Milliseconds(1000))
                  .ok());
  ASSERT_TRUE(
      store_.Set(Layer::kUser, all_types::kTestCapacityBytes, 4096U).ok());

  EXPECT_FALSE(store_.Get(all_types::kTestEnabled));
  EXPECT_EQ(store_.Get(all_types::kTestCount), -5);
  EXPECT_EQ(web_store_.Get(all_types::kTestRatioPercent), 99.5);
  EXPECT_EQ(store_.Get(all_types::kTestMode), TestMode::kManual);
  EXPECT_EQ(store_.Get(all_types::kTestLabel), "hello");
  EXPECT_EQ(store_.Get(all_types::kTestTags),
            (std::vector<std::string>{"a", "b"}));
  EXPECT_EQ(store_.Get(all_types::kTestTimeoutMs), absl::Milliseconds(1000));
  EXPECT_EQ(store_.Get(all_types::kTestCapacityBytes), 4096U);
}

TEST_F(AllTypesTest, OutOfRangeValuesOfEveryNumericTypeAreRejected) {
  EXPECT_TRUE(absl::IsInvalidArgument(
      store_.Set(Layer::kUser, all_types::kTestCount, 6)));
  EXPECT_TRUE(absl::IsInvalidArgument(
      web_store_.Set(Layer::kUser, all_types::kTestRatioPercent, 100.5)));
  EXPECT_TRUE(absl::IsInvalidArgument(
      web_store_.Set(Layer::kUser, all_types::kTestRatioPercent,
                     std::numeric_limits<double>::quiet_NaN())));
  EXPECT_TRUE(absl::IsInvalidArgument(
      web_store_.Set(Layer::kUser, all_types::kTestRatioPercent,
                     std::numeric_limits<double>::infinity())));
  EXPECT_TRUE(absl::IsInvalidArgument(store_.Set(
      Layer::kUser, all_types::kTestTimeoutMs, absl::Milliseconds(1001))));
  EXPECT_TRUE(absl::IsInvalidArgument(
      store_.Set(Layer::kUser, all_types::kTestCapacityBytes, 4097U)));
  EXPECT_TRUE(absl::IsInvalidArgument(
      store_.Set(Layer::kUser, all_types::kTestCapacityBytes,
                 std::numeric_limits<std::uint64_t>::max())));
  EXPECT_TRUE(absl::IsInvalidArgument(store_.Set(
      Layer::kUser, all_types::kTestMode, static_cast<TestMode>(7))));
  EXPECT_EQ(store_.Get(all_types::kTestCount), 2);
  EXPECT_EQ(store_.Get(all_types::kTestCapacityBytes), 1024U);
}

TEST_F(AllTypesTest, StringAssignmentsSetEveryType) {
  const auto set = [&](const char* assignment) {
    EXPECT_TRUE(store_.SetFromAssignment(Layer::kUser, assignment).ok())
        << assignment;
  };
  set("test.enabled=false");
  set("test.count=-3");
  set("test.mode=manual");
  set("test.label=a=b");
  set("test.tags=x,y,z");
  set("test.timeout_ms=250");
  set("test.capacity_bytes=2048");
  EXPECT_FALSE(store_.Get(all_types::kTestEnabled));
  EXPECT_EQ(store_.Get(all_types::kTestCount), -3);
  EXPECT_EQ(store_.Get(all_types::kTestMode), TestMode::kManual);
  EXPECT_EQ(store_.Get(all_types::kTestLabel), "a=b");
  EXPECT_EQ(store_.Get(all_types::kTestTags),
            (std::vector<std::string>{"x", "y", "z"}));
  EXPECT_EQ(store_.Get(all_types::kTestTimeoutMs), absl::Milliseconds(250));
  EXPECT_EQ(store_.Get(all_types::kTestCapacityBytes), 2048U);
  ASSERT_TRUE(
      web_store_.SetFromAssignment(Layer::kUser, "test.ratio_percent=12.25")
          .ok());
  EXPECT_EQ(web_store_.Get(all_types::kTestRatioPercent), 12.25);
}

TEST_F(AllTypesTest, FormerNamesResolveToTheCurrentKey) {
  ASSERT_TRUE(
      store_.SetFromAssignment(Layer::kUser, "test.old_timeout_ms=300").ok());
  EXPECT_EQ(store_.Get(all_types::kTestTimeoutMs), absl::Milliseconds(300));
}

TEST_F(AllTypesTest, KeysWithoutTheCurrentPlatformAreIgnoredButStillValidated) {
  EXPECT_FALSE(store_.IsSupported(all_types::kTestRatioPercent));
  EXPECT_FALSE(web_store_.IsSupported(all_types::kTestCount));
  EXPECT_TRUE(
      store_.Set(Layer::kUser, all_types::kTestRatioPercent, 10.0).ok());
  EXPECT_EQ(store_.Get(all_types::kTestRatioPercent), 50.0);
  EXPECT_TRUE(absl::IsInvalidArgument(
      store_.Set(Layer::kUser, all_types::kTestRatioPercent, 500.0)));
}

}
}
