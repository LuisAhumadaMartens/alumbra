#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "simulator.h"

namespace umbra::abr::simulation {
namespace {

TEST(AbrSimulationTest, SyntheticTraceReportsAreByteDeterministic) {
  const std::filesystem::path trace_directory =
      std::filesystem::path(ALUMBRA_SOURCE_DIR) / "test/umbra/abr/traces";
  const std::vector<std::string> trace_names = {"step_drop", "oscillation",
                                                "ramp", "outage"};
  const std::vector<Baseline> baselines =
      ReadBaseline(std::filesystem::path(ALUMBRA_SOURCE_DIR) /
                   "test/umbra/abr/baseline.csv");
  for (const std::string& trace_name : trace_names) {
    const Trace trace = ReadTrace(trace_directory / (trace_name + ".csv"));
    const Result first = ::umbra::abr::simulation::Run(trace);
    const Result second = ::umbra::abr::simulation::Run(trace);
    EXPECT_EQ(RenderJson(first), RenderJson(second));
    EXPECT_EQ(RenderMarkdown(first), RenderMarkdown(second));
    EXPECT_NE(RenderJson(first).find("\"rebuffer_ratio\""), std::string::npos);
    EXPECT_NE(RenderJson(first).find("\"bitrate_efficiency\""),
              std::string::npos);
    EXPECT_NE(RenderMarkdown(first).find("Rebuffer ratio"), std::string::npos);
    EXPECT_NE(RenderMarkdown(first).find("Bitrate efficiency"),
              std::string::npos);
    EXPECT_TRUE(std::isfinite(first.metrics.time_weighted_bitrate_bps));
    EXPECT_TRUE(std::isfinite(first.metrics.qoe_score));
    const auto baseline =
        std::ranges::find(baselines, trace_name, &Baseline::trace_name);
    ASSERT_NE(baseline, baselines.end());
    const Evaluation first_evaluation = Evaluate(first, *baseline);
    const Evaluation second_evaluation = Evaluate(second, *baseline);
    EXPECT_TRUE(first_evaluation.passed);
    EXPECT_EQ(RenderEvaluationMarkdown(first_evaluation),
              RenderEvaluationMarkdown(second_evaluation));
  }
}

TEST(AbrSimulationTest, ParsesMahimahiPacketDeliveryTimes) {
  const Trace trace = ReadTraceText("packet_trace", "0\n1\n1000\n", true);
  ASSERT_EQ(trace.points.size(), 2U);
  EXPECT_EQ(trace.points[0].time_ms, 0);
  EXPECT_EQ(trace.points[0].throughput_bps, 24000);
  EXPECT_EQ(trace.points[1].time_ms, 1000);
  EXPECT_EQ(trace.points[1].throughput_bps, 12000);
}

TEST(AbrSimulationTest, RejectsMalformedCsv) {
  EXPECT_THROW(ReadTraceText("bad", "time_ms,throughput_bps\n1,-2\n", false),
               std::runtime_error);
}

TEST(AbrSimulationTest, AppliesAllRegressionThresholds) {
  const Baseline baseline{
      .trace_name = "sample",
      .rebuffer_ratio = 0.01,
      .bitrate_efficiency = 0.8,
      .switches_per_minute = 2.0,
      .thresholds = Thresholds{.max_rebuffer_ratio = 0.011,
                               .min_bitrate_efficiency = 0.72,
                               .max_switches_per_minute = 2.2}};
  const Result passing{.trace_name = "sample",
                       .configuration_name = "default",
                       .metrics = Metrics{.rebuffer_count = 1,
                                          .rebuffer_ms = 100,
                                          .startup_delay_ms = 200,
                                          .switch_count = 2,
                                          .time_weighted_bitrate_bps = 800000,
                                          .qoe_score = 1,
                                          .rebuffer_ratio = 0.011,
                                          .bitrate_efficiency = 0.72,
                                          .switches_per_minute = 2.2,
                                          .sustainable_bitrate_bps = 1000000}};
  EXPECT_TRUE(Evaluate(passing, baseline).passed);
  Result rebuffer_failing = passing;
  rebuffer_failing.metrics.rebuffer_ratio = 0.0111;
  EXPECT_FALSE(Evaluate(rebuffer_failing, baseline).passed);
  Result bitrate_failing = passing;
  bitrate_failing.metrics.bitrate_efficiency = 0.719;
  EXPECT_FALSE(Evaluate(bitrate_failing, baseline).passed);
  Result switch_failing = passing;
  switch_failing.metrics.switches_per_minute = 2.21;
  const Evaluation evaluation = Evaluate(switch_failing, baseline);
  EXPECT_FALSE(evaluation.passed);
  EXPECT_NE(RenderEvaluationMarkdown(evaluation).find("| FAIL |"),
            std::string::npos);
}

}
}
