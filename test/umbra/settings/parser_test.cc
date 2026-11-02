#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"
#include "umbra/base/url_redactor.h"
#include "umbra/settings/setting_parser.h"
#include "umbra/settings/setting_types.h"
#include "umbra/settings/setting_value.h"
#include "umbra/settings/settings_keys.h"
#include "umbra/settings/testing/all_types_keys.h"

namespace umbra::settings {
namespace {

const base::UrlRedactor kRedactor;

const SettingInfo& Info(const Registry& registry, std::size_t index) {
  return registry.settings[index];
}

TEST(ParseAssignmentTest, SplitsAtTheFirstEqualsSign) {
  const auto parsed = ParseAssignment("a.b=c=d", kRedactor);
  ASSERT_TRUE(parsed.ok());
  EXPECT_EQ(parsed->key, "a.b");
  EXPECT_EQ(parsed->value, "c=d");
  const auto empty = ParseAssignment("a.b=", kRedactor);
  ASSERT_TRUE(empty.ok());
  EXPECT_TRUE(empty->value.empty());
}

TEST(ParseAssignmentTest, RejectsMissingKeyOrSeparator) {
  EXPECT_TRUE(absl::IsInvalidArgument(
      ParseAssignment("no separator", kRedactor).status()));
  EXPECT_TRUE(
      absl::IsInvalidArgument(ParseAssignment("=1", kRedactor).status()));
  EXPECT_TRUE(absl::IsInvalidArgument(ParseAssignment("", kRedactor).status()));
}

TEST(ParseValueTest, ParsesEveryTypeFromText) {
  const Registry registry = all_types::GetRegistry();
  const auto flag = ParseValue(Info(registry, all_types::kTestEnabled.Index()),
                               "false", kRedactor);
  ASSERT_TRUE(flag.ok());
  EXPECT_EQ(std::get<bool>(*flag), false);

  const auto number = ParseValue(Info(registry, all_types::kTestCount.Index()),
                                 "-4", kRedactor);
  ASSERT_TRUE(number.ok());
  EXPECT_EQ(std::get<std::int64_t>(*number), -4);

  const auto ratio = ParseValue(
      Info(registry, all_types::kTestRatioPercent.Index()), "12.5", kRedactor);
  ASSERT_TRUE(ratio.ok());
  EXPECT_EQ(std::get<double>(*ratio), 12.5);

  const auto mode = ParseValue(Info(registry, all_types::kTestMode.Index()),
                               "manual", kRedactor);
  ASSERT_TRUE(mode.ok());
  EXPECT_EQ(std::get<std::string>(*mode), "manual");

  const auto tags = ParseValue(Info(registry, all_types::kTestTags.Index()),
                               "x,y", kRedactor);
  ASSERT_TRUE(tags.ok());
  EXPECT_EQ(std::get<std::vector<std::string>>(*tags),
            (std::vector<std::string>{"x", "y"}));
  const auto none =
      ParseValue(Info(registry, all_types::kTestTags.Index()), "", kRedactor);
  ASSERT_TRUE(none.ok());
  EXPECT_TRUE(std::get<std::vector<std::string>>(*none).empty());
}

TEST(ParseValueTest, RejectionsNameTheKeyTheValueAndTheAllowedRange) {
  const Registry registry = all_types::GetRegistry();
  const auto rejected =
      ParseValue(Info(registry, all_types::kTestCount.Index()), "9", kRedactor);
  ASSERT_TRUE(absl::IsInvalidArgument(rejected.status()));
  const std::string message(rejected.status().message());
  EXPECT_NE(message.find("test.count"), std::string::npos) << message;
  EXPECT_NE(message.find("=9"), std::string::npos) << message;
  EXPECT_NE(message.find("-5 to 5"), std::string::npos) << message;

  const auto enum_rejected = ParseValue(
      Info(registry, all_types::kTestMode.Index()), "turbo", kRedactor);
  ASSERT_TRUE(absl::IsInvalidArgument(enum_rejected.status()));
  const std::string enum_message(enum_rejected.status().message());
  EXPECT_NE(enum_message.find("test.mode"), std::string::npos);
  EXPECT_NE(enum_message.find("turbo"), std::string::npos);
  EXPECT_NE(enum_message.find("auto, manual"), std::string::npos);
}

TEST(ParseValueTest, RejectsMalformedAndNonFiniteText) {
  const Registry registry = all_types::GetRegistry();
  const auto reject = [&](std::size_t index, std::string_view text) {
    EXPECT_TRUE(absl::IsInvalidArgument(
        ParseValue(Info(registry, index), text, kRedactor).status()))
        << text;
  };
  for (const char* text : {"", "TRUE", "1", "yes", " true"}) {
    reject(all_types::kTestEnabled.Index(), text);
  }
  for (const char* text : {"", "+1", " 1", "1 ", "1.0", "0x1", "abc",
                           "99999999999999999999", "6", "-6"}) {
    reject(all_types::kTestCount.Index(), text);
  }
  for (const char* text : {"", "nan", "inf", "-inf", " 1.0", "1.0 ", "101",
                           "-0.5", "1e999", "x"}) {
    reject(all_types::kTestRatioPercent.Index(), text);
  }
  for (const char* text : {"a,,b", ",a", "a,"}) {
    reject(all_types::kTestTags.Index(), text);
  }
  reject(all_types::kTestTimeoutMs.Index(), "1001");
  reject(all_types::kTestCapacityBytes.Index(), "-1");
  reject(all_types::kTestCapacityBytes.Index(), "4097");
}

TEST(ParseValueTest, FormattedValuesParseBackToTheSameValue) {
  const Registry registry = GetRegistry();
  for (const SettingInfo& info : registry.settings) {
    const Value defaults = DefaultValue(info);
    EXPECT_FALSE(FindViolation(info, defaults).has_value()) << info.name;
    const auto parsed = ParseValue(info, FormatValue(defaults), kRedactor);
    ASSERT_TRUE(parsed.ok()) << info.name << ": " << parsed.status();
    EXPECT_EQ(*parsed, defaults) << info.name;
  }
}

}
}
