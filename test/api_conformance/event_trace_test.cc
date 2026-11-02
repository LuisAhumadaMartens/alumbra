#include "event_trace.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "element_harness.h"
#include "gtest/gtest.h"

namespace umbra::player::conformance {
namespace {

using Names = std::vector<std::string>;

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream stream(path);
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

std::vector<std::filesystem::path> TraceFiles() {
  std::vector<std::filesystem::path> files;
  for (const auto& entry :
       std::filesystem::directory_iterator(ALUMBRA_TRACE_DIRECTORY)) {
    if (entry.path().extension() == ".trace") {
      files.push_back(entry.path());
    }
  }
  std::ranges::sort(files);
  return files;
}

std::string ParameterName(
    const ::testing::TestParamInfo<std::filesystem::path>& info) {
  std::string name = info.param.stem().string();
  std::ranges::replace(name, '-', '_');
  return name;
}

class TraceReplayTest
    : public ElementHarness,
      public ::testing::WithParamInterface<std::filesystem::path> {};

TEST_P(TraceReplayTest, ReplaysWithoutDifference) {
  EventTrace trace;
  std::string error;
  ASSERT_TRUE(ParseEventTrace(ReadFile(GetParam()), trace, error)) << error;
  EXPECT_EQ(trace.name, GetParam().stem().string());
  StartElement();
  size_t index = 0;
  for (const TraceStep& step : trace.steps) {
    ++index;
    ASSERT_TRUE(RunStep(step, error)) << error;
    Names actual = Pull();
    for (const std::string& ignored : trace.ignored) {
      std::erase(actual, ignored);
    }
    const std::string difference = CompareEventSequences(step.expected, actual);
    EXPECT_EQ(difference, "")
        << trace.name << " step " << index << " (" << step.action << ")";
  }
}

INSTANTIATE_TEST_SUITE_P(RecordedTraces, TraceReplayTest,
                         ::testing::ValuesIn(TraceFiles()), ParameterName);

TEST(EventTraceTest, TheTraceDirectoryIsNotEmpty) {
  EXPECT_GE(TraceFiles().size(), 8U);
}

TEST(EventTraceTest, EveryTraceNamesItsProvenance) {
  for (const auto& path : TraceFiles()) {
    EventTrace trace;
    std::string error;
    ASSERT_TRUE(ParseEventTrace(ReadFile(path), trace, error)) << error;
    EXPECT_TRUE(trace.provenance.rfind("html-spec ", 0) == 0 ||
                trace.provenance.rfind("chrome-recorded ", 0) == 0)
        << path;
  }
}

TEST(EventTraceTest, ParsesStepsExpectationsAndIgnoredEvents) {
  EventTrace trace;
  std::string error;
  ASSERT_TRUE(ParseEventTrace(
      "scenario: s\nprovenance: html-spec x\nignore: progress\n\n"
      "step: set_src 3000\nexpect: loadstart\nstep: open\nexpect:\n",
      trace, error))
      << error;
  EXPECT_EQ(trace.name, "s");
  EXPECT_EQ(trace.ignored, Names{"progress"});
  ASSERT_EQ(trace.steps.size(), 2U);
  EXPECT_EQ(trace.steps[0].action, "set_src");
  EXPECT_EQ(trace.steps[0].arguments, Names{"3000"});
  EXPECT_EQ(trace.steps[0].expected, Names{"loadstart"});
  EXPECT_TRUE(trace.steps[1].expected.empty());
}

TEST(EventTraceTest, RejectsMalformedTraces) {
  EventTrace trace;
  std::string error;
  EXPECT_FALSE(ParseEventTrace("nonsense\n", trace, error));
  EXPECT_FALSE(ParseEventTrace("bogus: 1\n", trace, error));
  EXPECT_FALSE(ParseEventTrace("expect: a\n", trace, error));
  EXPECT_FALSE(ParseEventTrace(
      "scenario: s\nprovenance: p\nstep: a\nstep: b\nexpect:\n", trace, error));
  EXPECT_FALSE(
      ParseEventTrace("scenario: s\nprovenance: p\nstep: a\n", trace, error));
  EXPECT_FALSE(ParseEventTrace("scenario: s\n", trace, error));
}

TEST(EventTraceTest, TheComparatorReportsTheFirstDifference) {
  EXPECT_EQ(CompareEventSequences({"a", "b"}, {"a", "b"}), "");
  const std::string difference = CompareEventSequences(
      {"seeking", "timeupdate", "seeked"}, {"seeking", "seeked", "timeupdate"});
  EXPECT_NE(difference, "");
  EXPECT_NE(difference.find("position 1"), std::string::npos);
  EXPECT_NE(CompareEventSequences({"a"}, {"a", "b"}), "");
  EXPECT_NE(CompareEventSequences({"a", "b"}, {"a"}), "");
}

}
}
