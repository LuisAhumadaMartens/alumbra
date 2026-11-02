#include "umbra/abr/bandwidth_estimator.h"

#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "umbra/observability/metrics.h"

namespace umbra::abr {
namespace {

constexpr std::uint64_t kSampleBytes = 1000000;

class CapturingMetricsSink final : public observability::MetricsSink {
 public:
  void OnMetricsEvent(const observability::MetricsRecord& record) override {
    records.push_back(record);
  }

  std::vector<observability::MetricsRecord> records;
};

BandwidthEstimatorConfig TestConfig() {
  return {.default_bandwidth_bps = 4000000,
          .fast_half_life_ms = 1000,
          .slow_half_life_ms = 5000,
          .min_sample_bytes = 1024};
}

void AddThroughput(BandwidthEstimator& estimator, std::int64_t bps,
                   absl::Duration ttfb = absl::Milliseconds(100)) {
  const auto transfer = absl::Seconds(static_cast<double>(kSampleBytes) * 8.0 /
                                      static_cast<double>(bps));
  estimator.AddSample(kSampleBytes, ttfb, transfer);
}

TEST(BandwidthEstimatorTest, StartsWithConfiguredEstimate) {
  const auto estimator = CreateBandwidthEstimator(TestConfig());
  const BandwidthEstimate estimate = estimator->GetEstimate();
  EXPECT_EQ(estimate.bandwidth_bps, 4000000);
  EXPECT_EQ(estimate.sample_count, 0U);
  EXPECT_FALSE(estimate.has_samples);
}

TEST(BandwidthEstimatorTest, IgnoresSmallAndInvalidSamples) {
  auto estimator = CreateBandwidthEstimator(TestConfig());
  estimator->AddSample(1023, absl::Milliseconds(10), absl::Milliseconds(1));
  estimator->AddSample(kSampleBytes, absl::Milliseconds(10),
                       absl::ZeroDuration());
  estimator->AddSample(kSampleBytes, absl::Milliseconds(-1), absl::Seconds(1));
  EXPECT_EQ(estimator->GetEstimate().bandwidth_bps, 4000000);
  EXPECT_EQ(estimator->GetEstimate().sample_count, 0U);
}

TEST(BandwidthEstimatorTest, IgnoresCacheHits) {
  auto estimator = CreateBandwidthEstimator(TestConfig());
  estimator->AddSample(kSampleBytes, absl::Milliseconds(1),
                       absl::Milliseconds(1), true);
  EXPECT_EQ(estimator->GetEstimate().bandwidth_bps, 4000000);
  EXPECT_EQ(estimator->GetEstimate().sample_count, 0U);
}

TEST(BandwidthEstimatorTest, IgnoresManifestAndKeySizedRequests) {
  BandwidthEstimatorConfig config = TestConfig();
  config.min_sample_bytes = 16384;
  auto estimator = CreateBandwidthEstimator(config);
  estimator->AddSample(2000, absl::Milliseconds(50), absl::Milliseconds(5));
  estimator->AddSample(8000, absl::Milliseconds(50), absl::Milliseconds(20));
  EXPECT_EQ(estimator->GetEstimate().bandwidth_bps, 4000000);
  EXPECT_EQ(estimator->GetEstimate().sample_count, 0U);
}

TEST(BandwidthEstimatorTest, ConstantThroughputConverges) {
  auto estimator = CreateBandwidthEstimator(TestConfig());
  for (int i = 0; i < 12; ++i) {
    AddThroughput(*estimator, 8000000);
  }
  EXPECT_NEAR(estimator->GetEstimate().bandwidth_bps, 8000000, 100000);
}

TEST(BandwidthEstimatorTest, StepDownReactsWithinBoundedSamples) {
  auto estimator = CreateBandwidthEstimator(TestConfig());
  for (int i = 0; i < 8; ++i) {
    AddThroughput(*estimator, 16000000);
  }
  const std::int64_t before = estimator->GetEstimate().bandwidth_bps;
  for (int i = 0; i < 5; ++i) {
    AddThroughput(*estimator, 2000000);
  }
  const std::int64_t after = estimator->GetEstimate().bandwidth_bps;
  EXPECT_LT(after, before / 2);
  EXPECT_GT(after, 2000000);
}

TEST(BandwidthEstimatorTest, StepUpRaisesEstimateWithinBoundedSamples) {
  auto estimator = CreateBandwidthEstimator(TestConfig());
  AddThroughput(*estimator, 2000000);
  for (int i = 0; i < 5; ++i) {
    AddThroughput(*estimator, 16000000);
  }
  EXPECT_GT(estimator->GetEstimate().bandwidth_bps, 4000000);
  EXPECT_LT(estimator->GetEstimate().bandwidth_bps, 16000000);
}

TEST(BandwidthEstimatorTest, FastEstimateTracksBurstsAndSlowEstimateSmooths) {
  auto estimator = CreateBandwidthEstimator(TestConfig());
  AddThroughput(*estimator, 2000000);
  const std::int64_t steady = estimator->GetEstimate().bandwidth_bps;
  AddThroughput(*estimator, 20000000);
  EXPECT_GT(estimator->GetEstimate().bandwidth_bps, steady);
  EXPECT_LT(estimator->GetEstimate().bandwidth_bps, 20000000);
}

TEST(BandwidthEstimatorTest, ChunkSamplesUseActiveTransferDuration) {
  auto estimator = CreateBandwidthEstimator(TestConfig());
  estimator->AddChunkSample(kSampleBytes, absl::Milliseconds(300),
                            absl::Milliseconds(500));
  const BandwidthEstimate estimate = estimator->GetEstimate();
  EXPECT_EQ(estimate.bandwidth_bps, 16000000);
  EXPECT_EQ(estimate.ttfb_ms, 300);
  EXPECT_EQ(estimate.sample_count, 1U);
}

TEST(BandwidthEstimatorTest, PublishesEstimateForQoeMetrics) {
  auto sink = std::make_shared<CapturingMetricsSink>();
  observability::SetMetricsSink(sink);
  auto estimator = CreateBandwidthEstimator(TestConfig());
  AddThroughput(*estimator, 8000000);
  observability::SetMetricsSink(nullptr);
  ASSERT_EQ(sink->records.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<observability::BandwidthEstimateMetric>(
      sink->records.front().event));
  const auto& metric = std::get<observability::BandwidthEstimateMetric>(
      sink->records.front().event);
  EXPECT_EQ(metric.bandwidth_bps, 8000000);
  EXPECT_EQ(metric.sample_count, 1U);
}

}
}
