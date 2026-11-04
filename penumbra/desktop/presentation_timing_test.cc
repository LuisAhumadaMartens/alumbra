#include "penumbra/desktop/video/presentation_timing.h"

#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "penumbra/interface/rational.h"

namespace penumbra::desktop {
namespace {

TEST(PresentationTimingTest, DisablesRateMatchingExplicitly) {
  const DisplayRateMatchPlan plan =
      PlanDisplayRateMatch(DisplayRateMatch::kOff, Rational::Of(24, 1),
                           Rational::Of(60, 1), {}, false, false);

  EXPECT_EQ(plan.status, DisplayRateMatchStatus::kOff);
  EXPECT_FALSE(plan.requested_rate.has_value());
  EXPECT_FALSE(plan.reason.empty());
}

TEST(PresentationTimingTest, RecognisesAnExistingExactCadence) {
  const DisplayRateMatchPlan plan =
      PlanDisplayRateMatch(DisplayRateMatch::kSeamless, Rational::Of(24, 1),
                           Rational::Of(120, 1), {}, false, false);

  EXPECT_EQ(plan.status, DisplayRateMatchStatus::kAlreadyMatched);
  ASSERT_TRUE(plan.requested_rate.has_value());
  EXPECT_EQ(*plan.requested_rate, Rational::Of(120, 1));
}

TEST(PresentationTimingTest, RequestsTheFirstSeamlessExactMultiple) {
  const DisplayRateMatchPlan plan = PlanDisplayRateMatch(
      DisplayRateMatch::kSeamless, Rational::Of(24, 1), Rational::Of(60, 1),
      {Rational::Of(48, 1), Rational::Of(72, 1), Rational::Of(120, 1)}, true,
      false);

  EXPECT_EQ(plan.status, DisplayRateMatchStatus::kRequestPending);
  ASSERT_TRUE(plan.requested_rate.has_value());
  EXPECT_EQ(*plan.requested_rate, Rational::Of(48, 1));
}

TEST(PresentationTimingTest, AllowsModeSwitchingOnlyForAlways) {
  const std::vector<Rational> rates = {Rational::Of(50, 1)};
  const DisplayRateMatchPlan seamless =
      PlanDisplayRateMatch(DisplayRateMatch::kSeamless, Rational::Of(25, 1),
                           Rational::Of(60, 1), rates, false, true);
  const DisplayRateMatchPlan always =
      PlanDisplayRateMatch(DisplayRateMatch::kAlways, Rational::Of(25, 1),
                           Rational::Of(60, 1), rates, false, true);

  EXPECT_EQ(seamless.status, DisplayRateMatchStatus::kUnsupported);
  EXPECT_EQ(always.status, DisplayRateMatchStatus::kRequestPending);
  ASSERT_TRUE(always.requested_rate.has_value());
  EXPECT_EQ(*always.requested_rate, Rational::Of(50, 1));
}

TEST(PresentationTimingTest, PreservesFractionalFrameRateCadence) {
  const DisplayRateMatchPlan plan = PlanDisplayRateMatch(
      DisplayRateMatch::kSeamless, Rational::Of(24000, 1001),
      Rational::Of(60, 1), {Rational::Of(120000, 1001)}, true, false);

  EXPECT_EQ(plan.status, DisplayRateMatchStatus::kRequestPending);
  ASSERT_TRUE(plan.requested_rate.has_value());
  EXPECT_EQ(*plan.requested_rate, Rational::Of(120000, 1001));
}

TEST(PresentationTimingTest, ReportsPlatformSpecificFallbacks) {
  const PlatformTimingState mac =
      DescribePlatformTiming(false, "macOS", "cocoa");
  const PlatformTimingState wayland =
      DescribePlatformTiming(false, "Linux", "wayland");
  const PlatformTimingState x11 = DescribePlatformTiming(false, "Linux", "x11");
  const PlatformTimingState offscreen =
      DescribePlatformTiming(true, "macOS", "cocoa");

  EXPECT_FALSE(mac.available);
  EXPECT_NE(mac.fallback_reason.find("CAMetalDisplayLink"), std::string::npos);
  EXPECT_NE(wayland.fallback_reason.find("presentation-time"),
            std::string::npos);
  EXPECT_NE(x11.fallback_reason.find("Present"), std::string::npos);
  EXPECT_NE(offscreen.fallback_reason.find("Offscreen"), std::string::npos);
}

}
}
