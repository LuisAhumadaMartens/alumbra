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
  for (const std::string& trace_name : trace_names) {
    const Trace trace = ReadTrace(trace_directory / (trace_name + ".csv"));
    const Result first = ::umbra::abr::simulation::Run(trace);
    const Result second = ::umbra::abr::simulation::Run(trace);
    EXPECT_EQ(RenderJson(first), RenderJson(second));
    EXPECT_EQ(RenderMarkdown(first), RenderMarkdown(second));
    EXPECT_TRUE(std::isfinite(first.metrics.time_weighted_bitrate_bps));
    EXPECT_TRUE(std::isfinite(first.metrics.qoe_score));
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

}
}
