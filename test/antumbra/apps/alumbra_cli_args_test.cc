#include <cstdint>
#include <string>
#include <vector>

#include "alumbra/cli/args.h"
#include "gtest/gtest.h"

namespace alumbra::cli {
namespace {

TEST(ArgumentsTest, ParsesRepeatableSettingsAndOptions) {
  const ParseResult result = ParseArguments(
      {"--headless", "--set", "network.http.read_timeout_ms=2000", "--set",
       "abr.enabled=false", "--start", "4.5", "--stats", "--log-level",
       "warning", "--trace", "player,buffer", "movie.mkv"});
  ASSERT_TRUE(result.error.empty());
  EXPECT_TRUE(result.options.headless);
  EXPECT_EQ(result.options.assignments.size(), 2U);
  EXPECT_EQ(result.options.start_seconds, 4.5);
  EXPECT_TRUE(result.options.stats);
  EXPECT_EQ(result.options.log_level, "warning");
  EXPECT_EQ(result.options.trace_categories, "player,buffer");
  EXPECT_EQ(result.options.source, "movie.mkv");
}

TEST(ArgumentsTest, RejectsMissingSource) {
  const ParseResult result = ParseArguments({"--headless"});
  EXPECT_EQ(result.error, "a URL or path is required");
}

TEST(ArgumentsTest, RejectsInvalidStartTime) {
  const ParseResult result = ParseArguments({"--start", "-1", "movie.mkv"});
  EXPECT_EQ(result.error, "--start requires a non-negative time in seconds");
}

TEST(SettingsTest, ListsGeneratedSettingsAndDefaults) {
  const std::string settings = ListSettings();
  EXPECT_NE(settings.find("network.http.read_timeout_ms=20000"),
            std::string::npos);
  EXPECT_NE(settings.find("abr.enabled=true"), std::string::npos);
}

TEST(SettingsTest, RejectsWrongTypeWithKeyValueAndExpectedType) {
  Options options;
  options.assignments.emplace_back("network.http.read_timeout_ms=abc");
  EXPECT_EQ(ValidateAssignments(options),
            "network.http.read_timeout_ms=abc expects a duration in integer "
            "milliseconds");
}

TEST(SettingsTest, RejectsUnknownKey) {
  Options options;
  options.assignments.emplace_back("unknown.key=1");
  EXPECT_EQ(ValidateAssignments(options), "unknown setting: unknown.key");
}

TEST(SettingsTest, ParsesAssignmentsIntoTypedValues) {
  Options options;
  options.assignments = {"abr.enabled=false", "buffer.start_threshold_ms=250",
                         "observability.trace.categories=player,buffer"};
  const AssignmentParseResult result = ParseAssignments(options);
  ASSERT_TRUE(result.error.empty());
  ASSERT_EQ(result.assignments.size(), 3U);
  EXPECT_EQ(std::get<bool>(result.assignments[0].value), false);
  EXPECT_EQ(std::get<std::int64_t>(result.assignments[1].value), 250);
  EXPECT_EQ(std::get<std::vector<std::string>>(result.assignments[2].value),
            (std::vector<std::string>{"player", "buffer"}));
}

}
}
