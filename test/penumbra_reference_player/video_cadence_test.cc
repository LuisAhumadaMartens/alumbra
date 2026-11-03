#include "penumbra/reference_player/video_cadence.h"

#include <cstdint>
#include <vector>

#include "gtest/gtest.h"
#include "penumbra/interface/rational.h"

namespace penumbra {
namespace {

TEST(VideoCadenceEstimatorTest, FindsCommonExactCadences) {
  VideoCadenceEstimator estimator;
  estimator.Observe(Rational::Of(24, 1), Rational::Of(60, 1), 0);
  estimator.Observe(Rational::Of(24, 1), Rational::Of(60, 1), 2'000'000);
  EXPECT_EQ(estimator.Cadence(), (std::vector<uint32_t>{2, 3}));
  estimator.Reset();
  estimator.Observe(Rational::Of(60, 1), Rational::Of(144, 1), 0);
  estimator.Observe(Rational::Of(60, 1), Rational::Of(144, 1), 2'000'000);
  EXPECT_EQ(estimator.Cadence(), (std::vector<uint32_t>{2, 3, 2, 3, 2}));
  estimator.Reset();
  estimator.Observe(Rational::Of(24'000, 1001), Rational::Of(120, 1), 0);
  estimator.Observe(Rational::Of(24'000, 1001), Rational::Of(120, 1),
                    2'000'000);
  EXPECT_EQ(estimator.Cadence(), (std::vector<uint32_t>{5}));
}

TEST(VideoCadenceEstimatorTest, RequiresStableCandidateBeforeSwitching) {
  VideoCadenceEstimator estimator;
  estimator.Observe(Rational::Of(24, 1), Rational::Of(60, 1), 0);
  EXPECT_TRUE(estimator.Cadence().empty());
  estimator.Observe(Rational::Of(24, 1), Rational::Of(60, 1), 1'999'999);
  EXPECT_TRUE(estimator.Cadence().empty());
  estimator.Observe(Rational::Of(24, 1), Rational::Of(60, 1), 2'000'000);
  EXPECT_EQ(estimator.Cadence(), (std::vector<uint32_t>{2, 3}));
  EXPECT_TRUE(estimator.Stable());
  estimator.Observe(Rational::Of(30, 1), Rational::Of(60, 1), 2'100'000);
  EXPECT_EQ(estimator.Cadence(), (std::vector<uint32_t>{2, 3}));
  estimator.Observe(Rational::Of(30, 1), Rational::Of(60, 1), 4'100'000);
  EXPECT_EQ(estimator.Cadence(), (std::vector<uint32_t>{2}));
  EXPECT_EQ(estimator.CadenceBreaks(), 1U);
}

TEST(VideoCadenceEstimatorTest, FasterContentHasNoOneFramePerRefreshCadence) {
  VideoCadenceEstimator estimator;
  estimator.Observe(Rational::Of(120, 1), Rational::Of(60, 1), 0);
  estimator.Observe(Rational::Of(120, 1), Rational::Of(60, 1), 2'000'000);
  EXPECT_TRUE(estimator.Cadence().empty());
  EXPECT_EQ(estimator.CadenceBreaks(), 0U);
}

}
}
