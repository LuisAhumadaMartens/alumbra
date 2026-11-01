#include "penumbra/interface/rational.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <random>
#include <vector>

#include "absl/numeric/int128.h"
#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

namespace penumbra {
namespace {

constexpr int64_t kInt64Max = std::numeric_limits<int64_t>::max();
constexpr int64_t kInt64Min = std::numeric_limits<int64_t>::min();

constexpr std::array<Rounding, 5> kAllRoundings = {
    Rounding::kTowardZero, Rounding::kAwayFromZero, Rounding::kDown,
    Rounding::kUp,         Rounding::kNearest,
};

const std::vector<Rational>& CommonTimeBases() {
  static const auto* const kTimeBases = new std::vector<Rational>{
      Rational::Of(1, 90'000),    Rational::Of(1, 48'000),
      Rational::Of(1, 44'100),    Rational::Of(1, 1'000'000),
      Rational::Of(1, 1'000),     Rational::Of(1001, 30'000),
      Rational::Of(1001, 24'000), Rational::Of(1001, 60'000),
      Rational::Of(1, 25),        Rational::Of(1, 1),
  };
  return *kTimeBases;
}

bool SatisfiesRounding(int64_t value, Rational from, Rational to,
                       Rounding rounding, int64_t result) {
  const absl::int128 exact_numerator = absl::int128(value) *
                                       absl::int128(from.numerator()) *
                                       absl::int128(to.denominator());
  const absl::int128 denominator =
      absl::int128(from.denominator()) * absl::int128(to.numerator());
  const absl::int128 scaled = absl::int128(result) * denominator;
  const bool floor_ok =
      scaled <= exact_numerator && exact_numerator < scaled + denominator;
  const bool ceil_ok =
      scaled - denominator < exact_numerator && exact_numerator <= scaled;
  switch (rounding) {
    case Rounding::kDown:
      return floor_ok;
    case Rounding::kUp:
      return ceil_ok;
    case Rounding::kTowardZero:
      return exact_numerator >= 0 ? floor_ok : ceil_ok;
    case Rounding::kAwayFromZero:
      return exact_numerator >= 0 ? ceil_ok : floor_ok;
    case Rounding::kNearest: {
      const absl::int128 error = scaled - exact_numerator;
      const absl::int128 twice = error < 0 ? -2 * error : 2 * error;
      if (twice > denominator) {
        return false;
      }
      if (twice == denominator) {
        return (error > 0) == (exact_numerator > 0);
      }
      return true;
    }
  }
  return false;
}

bool IsAtLeastAsFine(Rational candidate, Rational reference) {
  return absl::int128(candidate.numerator()) *
             absl::int128(reference.denominator()) <=
         absl::int128(reference.numerator()) *
             absl::int128(candidate.denominator());
}

std::vector<int64_t> SampleValues(uint64_t seed) {
  std::mt19937_64 generator(seed);
  std::uniform_int_distribution<int64_t> wide(-(int64_t{1} << 40),
                                              int64_t{1} << 40);
  std::uniform_int_distribution<int64_t> narrow(-100'000, 100'000);
  std::vector<int64_t> values = {0, 1, -1, 2, -2, 1'000, -1'000};
  for (int i = 0; i < 400; ++i) {
    values.push_back(wide(generator));
    values.push_back(narrow(generator));
  }
  std::ranges::sort(values);
  return values;
}

TEST(RationalTest, NormalisesSignAndCommonFactors) {
  EXPECT_EQ(Rational::Create(2, 4), Rational::Of(1, 2));
  EXPECT_EQ(Rational::Create(-1, -2), Rational::Of(1, 2));
  EXPECT_EQ(Rational::Create(1, -2), Rational::Of(-1, 2));
  EXPECT_EQ(Rational::Create(0, 5), Rational::Of(0, 1));
  EXPECT_EQ(Rational::Of(1001, 30'000).numerator(), 1001);
  EXPECT_EQ(Rational::Of(1001, 30'000).denominator(), 30'000);
}

TEST(RationalTest, RejectsInvalidValues) {
  EXPECT_FALSE(Rational::Create(1, 0).has_value());
  EXPECT_FALSE(Rational::Create(kInt64Min, 1).has_value());
  EXPECT_FALSE(Rational::Create(1, kInt64Min).has_value());
  EXPECT_TRUE(Rational::Create(kInt64Max, kInt64Max).has_value());
}

TEST(RationalTest, DefaultIsZeroAndNotATimeBase) {
  const Rational zero;
  EXPECT_EQ(zero, Rational::Of(0, 1));
  EXPECT_FALSE(zero.is_positive());
  EXPECT_FALSE(Rescale(1, zero, Rational::Of(1, 1), Rounding::kDown));
  EXPECT_FALSE(Rescale(1, Rational::Of(1, 1), zero, Rounding::kDown));
  EXPECT_FALSE(
      Rescale(1, Rational::Of(-1, 1), Rational::Of(1, 1), Rounding::kDown));
}

TEST(RationalTest, StringifiesForTests) {
  EXPECT_EQ(Rational::Of(1001, 30'000).ToString(), "1001/30000");
  EXPECT_EQ(absl::StrCat(Rational::Of(-1, 2)), "-1/2");
}

TEST(RescaleTest, ConvertsCommonTimeBasesExactly) {
  EXPECT_EQ(Rescale(90'000, Rational::Of(1, 90'000), Rational::Of(1, 1'000'000),
                    Rounding::kNearest),
            1'000'000);
  EXPECT_EQ(Rescale(3'003, Rational::Of(1, 90'000), Rational::Of(1001, 30'000),
                    Rounding::kNearest),
            1);
  EXPECT_EQ(Rescale(1, Rational::Of(1, 90'000), Rational::Of(1, 1'000'000),
                    Rounding::kDown),
            11);
  EXPECT_EQ(Rescale(1, Rational::Of(1, 90'000), Rational::Of(1, 1'000'000),
                    Rounding::kUp),
            12);
  EXPECT_EQ(Rescale(-1, Rational::Of(1, 90'000), Rational::Of(1, 1'000'000),
                    Rounding::kDown),
            -12);
  EXPECT_EQ(Rescale(-1, Rational::Of(1, 90'000), Rational::Of(1, 1'000'000),
                    Rounding::kTowardZero),
            -11);
}

TEST(RescaleTest, NearestBreaksTiesAwayFromZero) {
  EXPECT_EQ(
      Rescale(1, Rational::Of(1, 2), Rational::Of(1, 1), Rounding::kNearest),
      1);
  EXPECT_EQ(
      Rescale(-1, Rational::Of(1, 2), Rational::Of(1, 1), Rounding::kNearest),
      -1);
  EXPECT_EQ(
      Rescale(5, Rational::Of(1, 10), Rational::Of(1, 1), Rounding::kNearest),
      1);
  EXPECT_EQ(
      Rescale(4, Rational::Of(1, 10), Rational::Of(1, 1), Rounding::kNearest),
      0);
}

TEST(RescaleTest, MatchesExactRoundingForRandomValues) {
  const std::vector<int64_t> values = SampleValues(0xA1B0'0051);
  for (Rational const from : CommonTimeBases()) {
    for (Rational const to : CommonTimeBases()) {
      for (Rounding const rounding : kAllRoundings) {
        for (int64_t const value : values) {
          const std::optional<int64_t> result =
              Rescale(value, from, to, rounding);
          ASSERT_TRUE(result.has_value())
              << value << " " << from.ToString() << " -> " << to.ToString();
          ASSERT_TRUE(SatisfiesRounding(value, from, to, rounding, *result))
              << value << " " << from.ToString() << " -> " << to.ToString()
              << " gave " << *result;
        }
      }
    }
  }
}

TEST(RescaleTest, IsMonotonic) {
  const std::vector<int64_t> values = SampleValues(0x0051'0051);
  for (Rational const from : CommonTimeBases()) {
    for (Rational const to : CommonTimeBases()) {
      for (Rounding const rounding : kAllRoundings) {
        int64_t previous = std::numeric_limits<int64_t>::min();
        for (int64_t const value : values) {
          const int64_t result = Rescale(value, from, to, rounding).value();
          ASSERT_LE(previous, result)
              << value << " " << from.ToString() << " -> " << to.ToString();
          previous = result;
        }
      }
    }
  }
}

TEST(RescaleTest, RoundTripsWithinOneUnit) {
  const std::vector<int64_t> values = SampleValues(0x5EED);
  for (Rational const from : CommonTimeBases()) {
    for (Rational const to : CommonTimeBases()) {
      if (!IsAtLeastAsFine(to, from)) {
        continue;
      }
      for (Rounding const rounding : kAllRoundings) {
        for (int64_t const value : values) {
          const int64_t there = Rescale(value, from, to, rounding).value();
          const int64_t back = Rescale(there, to, from, rounding).value();
          ASSERT_LE(back > value ? back - value : value - back, 1)
              << value << " " << from.ToString() << " <-> " << to.ToString();
        }
      }
    }
  }
}

TEST(RescaleTest, HandlesMpegTsWrapRange) {
  constexpr int64_t kWrap = int64_t{1} << 33;
  const Rational ts = Rational::Of(1, 90'000);
  const Rational micros = Rational::Of(1, 1'000'000);
  for (int64_t const value : {kWrap - 1, kWrap, kWrap + 1, -kWrap, 2 * kWrap}) {
    for (Rounding const rounding : kAllRoundings) {
      const std::optional<int64_t> result =
          Rescale(value, ts, micros, rounding);
      ASSERT_TRUE(result.has_value());
      EXPECT_TRUE(SatisfiesRounding(value, ts, micros, rounding, *result));
      const std::optional<int64_t> back =
          Rescale(*result, micros, ts, rounding);
      ASSERT_TRUE(back.has_value());
    }
  }
}

TEST(RescaleTest, ReportsOverflowNearInt64LimitsWithoutUndefinedBehaviour) {
  const Rational one = Rational::Of(1, 1);
  const Rational half = Rational::Of(1, 2);
  const Rational ts = Rational::Of(1, 90'000);
  const Rational micros = Rational::Of(1, 1'000'000);
  for (Rounding const rounding : kAllRoundings) {
    EXPECT_EQ(Rescale(kInt64Max, one, one, rounding), kInt64Max);
    EXPECT_EQ(Rescale(kInt64Min, one, one, rounding), kInt64Min);
    EXPECT_EQ(Rescale(kInt64Min, micros, micros, rounding), kInt64Min);
    EXPECT_FALSE(Rescale(kInt64Max, one, half, rounding).has_value());
    EXPECT_FALSE(Rescale(kInt64Min, one, half, rounding).has_value());
    EXPECT_FALSE(Rescale(kInt64Max, ts, micros, rounding).has_value());
    EXPECT_FALSE(Rescale(kInt64Min, ts, micros, rounding).has_value());
    const std::optional<int64_t> down =
        Rescale(kInt64Max, micros, ts, rounding);
    ASSERT_TRUE(down.has_value());
    EXPECT_TRUE(SatisfiesRounding(kInt64Max, micros, ts, rounding, *down));
    const std::optional<int64_t> down_negative =
        Rescale(kInt64Min, micros, ts, rounding);
    ASSERT_TRUE(down_negative.has_value());
    EXPECT_TRUE(
        SatisfiesRounding(kInt64Min, micros, ts, rounding, *down_negative));
  }
  EXPECT_EQ(Rescale(kInt64Max - 1, half, one, Rounding::kUp), kInt64Max / 2);
  EXPECT_EQ(Rescale(kInt64Max, half, one, Rounding::kAwayFromZero),
            (kInt64Max / 2) + 1);
  EXPECT_EQ(Rescale(kInt64Min, half, one, Rounding::kDown), kInt64Min / 2);
}

TEST(RescaleTest, RejectsScalesThatDoNotFitSixtyFourBits) {
  const Rational huge = Rational::Of(kInt64Max, 1);
  const Rational tiny = Rational::Of(1, kInt64Max);
  EXPECT_FALSE(Rescale(1, huge, tiny, Rounding::kDown).has_value());
  EXPECT_FALSE(Rescale(1, tiny, huge, Rounding::kDown).has_value());
  EXPECT_EQ(Rescale(0, huge, huge, Rounding::kDown), 0);
}

TEST(RescaleTest, WorksInConstantExpressions) {
  static_assert(Rescale(90'000, Rational::Of(1, 90'000), Rational::Of(1, 1'000),
                        Rounding::kDown) == 1'000);
}

}
}
