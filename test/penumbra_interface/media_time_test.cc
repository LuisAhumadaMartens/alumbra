#include "penumbra/interface/media_time.h"

#include <cstdint>
#include <limits>
#include <optional>

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"
#include "penumbra/interface/rational.h"

namespace penumbra {
namespace {

constexpr int64_t kInt64Max = std::numeric_limits<int64_t>::max();
constexpr int64_t kInt64Min = std::numeric_limits<int64_t>::min();

TEST(TimeDeltaTest, ConstructsFromUnits) {
  EXPECT_EQ(TimeDelta::FromMilliseconds(1'500).InMicroseconds(), 1'500'000);
  EXPECT_EQ(TimeDelta::FromSeconds(-2).InMicroseconds(), -2'000'000);
  EXPECT_EQ(TimeDelta().InMicroseconds(), 0);
  EXPECT_TRUE(TimeDelta::Zero().is_finite());
}

TEST(TimeDeltaTest, SentinelsAreDistinct) {
  EXPECT_TRUE(TimeDelta::Unknown().is_unknown());
  EXPECT_FALSE(TimeDelta::Unknown().is_infinite());
  EXPECT_TRUE(TimeDelta::Infinite().is_infinite());
  EXPECT_TRUE(TimeDelta::NegativeInfinite().is_infinite());
  EXPECT_FALSE(TimeDelta::Infinite().is_finite());
  EXPECT_LT(TimeDelta::Unknown(), TimeDelta::NegativeInfinite());
  EXPECT_LT(TimeDelta::NegativeInfinite(), TimeDelta::FromSeconds(-1'000));
  EXPECT_LT(TimeDelta::FromSeconds(1'000), TimeDelta::Infinite());
}

TEST(TimeDeltaTest, SaturatesInsteadOfOverflowing) {
  EXPECT_EQ(TimeDelta::FromMicroseconds(kInt64Max), TimeDelta::Infinite());
  EXPECT_EQ(TimeDelta::FromMicroseconds(kInt64Min),
            TimeDelta::NegativeInfinite());
  EXPECT_EQ(TimeDelta::FromSeconds(kInt64Max), TimeDelta::Infinite());
  EXPECT_EQ(TimeDelta::FromSeconds(kInt64Min), TimeDelta::NegativeInfinite());
  const TimeDelta largest = TimeDelta::FromMicroseconds(kInt64Max - 1);
  EXPECT_TRUE(largest.is_finite());
  EXPECT_EQ(largest + TimeDelta::FromMicroseconds(1), TimeDelta::Infinite());
  EXPECT_EQ(-largest - TimeDelta::FromMicroseconds(1),
            TimeDelta::NegativeInfinite());
  EXPECT_EQ(-largest, TimeDelta::FromMicroseconds(kInt64Min + 2));
}

TEST(TimeDeltaTest, PropagatesUnknownAndInfinity) {
  const TimeDelta one = TimeDelta::FromSeconds(1);
  EXPECT_TRUE((TimeDelta::Unknown() + one).is_unknown());
  EXPECT_EQ(TimeDelta::Infinite() + one, TimeDelta::Infinite());
  EXPECT_EQ(TimeDelta::Infinite() - one, TimeDelta::Infinite());
  EXPECT_TRUE((TimeDelta::Infinite() - TimeDelta::Infinite()).is_unknown());
  EXPECT_EQ(TimeDelta::Infinite() + TimeDelta::Infinite(),
            TimeDelta::Infinite());
  EXPECT_EQ(-TimeDelta::Infinite(), TimeDelta::NegativeInfinite());
  EXPECT_TRUE((-TimeDelta::Unknown()).is_unknown());
}

TEST(MediaTimeTest, CombinesWithDeltas) {
  const MediaTime start = MediaTime::FromSeconds(10);
  const TimeDelta step = TimeDelta::FromMilliseconds(500);
  EXPECT_EQ((start + step).InMicroseconds(), 10'500'000);
  EXPECT_EQ((start - step).InMicroseconds(), 9'500'000);
  EXPECT_EQ(MediaTime::FromSeconds(12) - start, TimeDelta::FromSeconds(2));
  EXPECT_TRUE((MediaTime::Unknown() - start).is_unknown());
  EXPECT_EQ(MediaTime::Infinite() - start, TimeDelta::Infinite());
  EXPECT_TRUE((MediaTime::Infinite() - MediaTime::Infinite()).is_unknown());
  EXPECT_EQ(MediaTime::FromMicroseconds(kInt64Max - 1) -
                MediaTime::FromMicroseconds(-1),
            TimeDelta::Infinite());
}

TEST(MediaTimeTest, Stringifies) {
  EXPECT_EQ(absl::StrCat(MediaTime::FromMilliseconds(2)), "@2000us");
  EXPECT_EQ(absl::StrCat(MediaTime::Unknown()), "@unknown");
  EXPECT_EQ(absl::StrCat(TimeDelta::Infinite()), "+inf");
  EXPECT_EQ(absl::StrCat(TimeDelta::NegativeInfinite()), "-inf");
  EXPECT_EQ(absl::StrCat(Timestamp{3'003, Rational::Of(1, 90'000)}),
            "3003@1/90000");
  EXPECT_EQ(absl::StrCat(Timestamp{}), "unknown@0/1");
}

TEST(TimestampTest, ConvertsToMediaTimeWithExplicitRounding) {
  const Timestamp frame{.value = 1, .time_base = Rational::Of(1001, 30'000)};
  EXPECT_EQ(ToMediaTime(frame, Rounding::kDown),
            MediaTime::FromMicroseconds(33'366));
  EXPECT_EQ(ToMediaTime(frame, Rounding::kUp),
            MediaTime::FromMicroseconds(33'367));
  EXPECT_EQ(ToMediaTime(frame, Rounding::kNearest),
            MediaTime::FromMicroseconds(33'367));
  const Timestamp audio{.value = 48'000, .time_base = Rational::Of(1, 48'000)};
  EXPECT_EQ(ToTimeDelta(audio, Rounding::kNearest), TimeDelta::FromSeconds(1));
}

TEST(TimestampTest, KeepsUnknownAcrossConversions) {
  const Timestamp unknown{.value = Timestamp::kUnknown,
                          .time_base = Rational::Of(1, 90'000)};
  EXPECT_FALSE(unknown.is_known());
  EXPECT_EQ(ToMediaTime(unknown, Rounding::kNearest), MediaTime::Unknown());
  EXPECT_EQ(ToTimeDelta(unknown, Rounding::kNearest), TimeDelta::Unknown());
  const std::optional<Timestamp> rescaled =
      Rescale(unknown, Rational::Of(1, 48'000), Rounding::kNearest);
  ASSERT_TRUE(rescaled.has_value());
  EXPECT_FALSE(rescaled->is_known());
  EXPECT_EQ(rescaled->time_base, Rational::Of(1, 48'000));
  const std::optional<Timestamp> back = ToTimestamp(
      MediaTime::Unknown(), Rational::Of(1, 90'000), Rounding::kNearest);
  ASSERT_TRUE(back.has_value());
  EXPECT_FALSE(back->is_known());
}

TEST(TimestampTest, ConvertsFromMediaTime) {
  const std::optional<Timestamp> ts = ToTimestamp(
      MediaTime::FromSeconds(1), Rational::Of(1, 90'000), Rounding::kNearest);
  ASSERT_TRUE(ts.has_value());
  EXPECT_EQ(*ts, (Timestamp{90'000, Rational::Of(1, 90'000)}));
  EXPECT_FALSE(ToTimestamp(MediaTime::Infinite(), Rational::Of(1, 90'000),
                           Rounding::kNearest)
                   .has_value());
  EXPECT_FALSE(ToTimestamp(MediaTime::Zero(), Rational(), Rounding::kNearest)
                   .has_value());
  const std::optional<Timestamp> duration =
      ToTimestamp(TimeDelta::FromMilliseconds(20), Rational::Of(1, 48'000),
                  Rounding::kNearest);
  ASSERT_TRUE(duration.has_value());
  EXPECT_EQ(duration->value, 960);
}

TEST(TimestampTest, RejectsValuesOutsideTheFiniteRange) {
  const Timestamp huge{.value = kInt64Max, .time_base = Rational::Of(1, 1)};
  EXPECT_FALSE(ToMediaTime(huge, Rounding::kNearest).has_value());
  const Timestamp largest{.value = kInt64Max - 1,
                          .time_base = Rational::Of(1, 1'000'000)};
  EXPECT_TRUE(ToMediaTime(largest, Rounding::kNearest)->is_finite());
  const Timestamp too_large{.value = kInt64Max,
                            .time_base = Rational::Of(1, 1'000'000)};
  EXPECT_FALSE(ToMediaTime(too_large, Rounding::kNearest).has_value());
  const Timestamp negative{.value = kInt64Min + 1,
                           .time_base = Rational::Of(1, 1)};
  EXPECT_FALSE(
      Rescale(negative, Rational::Of(1, 2), Rounding::kNearest).has_value());
}

}
}
