#include "umbra/observability/log.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "capturing_sink.h"
#include "gtest/gtest.h"
#include "umbra/base/url_redactor.h"
#include "umbra/observability/category.h"

namespace umbra::observability {
namespace {

class LogTest : public ::testing::Test {
 protected:
  void SetUp() override {
    SetSink(sink_);
    SetRedactor(std::make_shared<const base::UrlRedactor>(
        std::array<std::uint64_t, 2>{1, 2}));
    SetLogLevel(Level::kInfo);
    SetLogCategoryMask(kAllCategories);
  }

  void TearDown() override {
    SetSink(nullptr);
    SetRedactor(nullptr);
    SetLogLevel(Level::kInfo);
    SetLogCategoryMask(kAllCategories);
  }

  std::shared_ptr<CapturingSink> sink_ = std::make_shared<CapturingSink>();
};

int Evaluate(int& counter) { return ++counter; }

TEST_F(LogTest, CapturesCategoryLevelMessageAndStructuredFields) {
  ALUMBRA_LOG(kNet, kWarning)
          .Field("throughput_bps", std::int64_t{4'000'000})
          .Field("buffer_ms", 1500U)
          .Field("ratio", 0.5)
          .Field("cached", true)
          .Field("host", "cdn.example.com")
      << "segment " << 7 << " slow";
  const auto records = sink_->Records();
  ASSERT_EQ(records.size(), 1U);
  EXPECT_EQ(records[0].category, Category::kNet);
  EXPECT_EQ(records[0].level, Level::kWarning);
  EXPECT_EQ(records[0].message, "segment 7 slow");
  ASSERT_EQ(records[0].fields.size(), 5U);
  EXPECT_EQ(records[0].fields[0].name, "throughput_bps");
  EXPECT_EQ(std::get<std::int64_t>(records[0].fields[0].value), 4'000'000);
  EXPECT_EQ(std::get<std::uint64_t>(records[0].fields[1].value), 1500U);
  EXPECT_EQ(std::get<double>(records[0].fields[2].value), 0.5);
  EXPECT_TRUE(std::get<bool>(records[0].fields[3].value));
  EXPECT_EQ(std::get<std::string>(records[0].fields[4].value),
            "cdn.example.com");
}

TEST_F(LogTest, ArgumentsAreNotEvaluatedWhenTheLevelIsFilteredOut) {
  int evaluations = 0;
  ALUMBRA_LOG(kAbr, kDebug).Field("evaluated", Evaluate(evaluations))
      << Evaluate(evaluations);
  EXPECT_EQ(evaluations, 0);
  EXPECT_TRUE(sink_->Records().empty());
  ALUMBRA_LOG(kAbr, kError).Field("evaluated", Evaluate(evaluations))
      << Evaluate(evaluations);
  EXPECT_EQ(evaluations, 2);
  EXPECT_EQ(sink_->Records().size(), 1U);
}

TEST_F(LogTest, ArgumentsAreNotEvaluatedWhenTheCategoryIsFilteredOut) {
  SetLogCategoryMask(CategoryBit(Category::kNet));
  int evaluations = 0;
  ALUMBRA_LOG(kBuffer, kError) << Evaluate(evaluations);
  EXPECT_EQ(evaluations, 0);
  ALUMBRA_LOG(kNet, kError) << Evaluate(evaluations);
  EXPECT_EQ(evaluations, 1);
  const auto records = sink_->Records();
  ASSERT_EQ(records.size(), 1U);
  EXPECT_EQ(records[0].category, Category::kNet);
}

TEST_F(LogTest, RuntimeLevelAndCategoryFiltersApply) {
  SetLogLevel(Level::kWarning);
  ALUMBRA_LOG(kPlayer, kInfo) << "dropped";
  ALUMBRA_LOG(kPlayer, kWarning) << "kept warning";
  ALUMBRA_LOG(kPlayer, kError) << "kept error";
  ASSERT_EQ(sink_->Records().size(), 2U);
  EXPECT_EQ(GetLogLevel(), Level::kWarning);
  SetLogCategoryMask(0);
  ALUMBRA_LOG(kPlayer, kError) << "dropped";
  EXPECT_EQ(sink_->Records().size(), 2U);
  EXPECT_EQ(GetLogCategoryMask(), 0U);
}

TEST_F(LogTest, EveryNLogsTheFirstAndEveryNthHit) {
  for (int i = 0; i < 10; ++i) {
    ALUMBRA_LOG_EVERY_N(kDemux, kError, 4).Field("index", i) << "hot";
  }
  const auto records = sink_->Records();
  ASSERT_EQ(records.size(), 3U);
  EXPECT_EQ(std::get<std::int64_t>(records[0].fields[0].value), 0);
  EXPECT_EQ(std::get<std::int64_t>(records[1].fields[0].value), 4);
  EXPECT_EQ(std::get<std::int64_t>(records[2].fields[0].value), 8);
}

TEST_F(LogTest, EveryNDoesNotAdvanceWhileFilteredOut) {
  SetLogLevel(Level::kError);
  int evaluations = 0;
  for (int i = 0; i < 5; ++i) {
    ALUMBRA_LOG_EVERY_N(kDemux, kInfo, Evaluate(evaluations)) << "hot";
  }
  EXPECT_EQ(evaluations, 0);
}

TEST_F(LogTest, EveryMsAdmitsOneMessageWithinTheInterval) {
  for (int i = 0; i < 50; ++i) {
    ALUMBRA_LOG_EVERY_MS(kRender, kError, 3'600'000) << "once";
  }
  EXPECT_EQ(sink_->Records().size(), 1U);
  for (int i = 0; i < 3; ++i) {
    ALUMBRA_LOG_EVERY_MS(kRender, kError, 0) << "always";
  }
  EXPECT_EQ(sink_->Records().size(), 4U);
}

TEST_F(LogTest, InvalidFieldNamesAreReplaced) {
  ALUMBRA_LOG(kText, kError).Field("BadName", 1).Field("good_name_ms", 2);
  const auto records = sink_->Records();
  ASSERT_EQ(records.size(), 1U);
  EXPECT_EQ(records[0].fields[0].name, "invalid_field_name");
  EXPECT_EQ(records[0].fields[1].name, "good_name_ms");
  EXPECT_TRUE(internal::IsLowerSnakeCase("throughput_bps"));
  EXPECT_FALSE(internal::IsLowerSnakeCase("Throughput"));
  EXPECT_FALSE(internal::IsLowerSnakeCase("a__b"));
  EXPECT_FALSE(internal::IsLowerSnakeCase("trailing_"));
  EXPECT_FALSE(internal::IsLowerSnakeCase(""));
}

TEST_F(LogTest, RedactsUrlsAndCredentialsInMessagesAndFields) {
  ALUMBRA_LOG(kNet, kError)
          .Field(
              "segment_url",
              "https://user:pw@cdn.example.com/a/tok123/seg.m4s?sig=abc#frag")
          .Field("authorization", "Bearer abc")
          .Field("license_body", "challenge-bytes")
      << "failed https://cdn.example.com/x/secret?token=zzz and Cookie: sid=9";
  const auto records = sink_->Records();
  ASSERT_EQ(records.size(), 1U);
  std::string everything = records[0].message;
  for (const LogField& field : records[0].fields) {
    if (const auto* text = std::get_if<std::string>(&field.value)) {
      everything += *text;
    }
  }
  for (const std::string secret :
       {"pw", "tok123", "seg.m4s", "sig=abc", "frag", "Bearer", "challenge",
        "secret", "zzz", "sid=9"}) {
    EXPECT_EQ(everything.find(secret), std::string::npos) << secret;
  }
  EXPECT_NE(everything.find("https://cdn.example.com [url:"),
            std::string::npos);
}

TEST_F(LogTest, EachThreadSeesItsOwnMessagesInProgramOrder) {
  constexpr int kThreads = 4;
  constexpr int kMessages = 200;
  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([] {
      for (int i = 0; i < kMessages; ++i) {
        ALUMBRA_LOG(kPort, kError).Field("index", i) << "tick";
      }
    });
  }
  for (std::thread& thread : threads) {
    thread.join();
  }
  const auto records = sink_->Records();
  ASSERT_EQ(records.size(), static_cast<std::size_t>(kThreads * kMessages));
  std::map<std::uint32_t, std::int64_t> next_index;
  std::map<std::uint32_t, std::uint64_t> last_sequence;
  for (const LogRecord& record : records) {
    const std::int64_t index = std::get<std::int64_t>(record.fields[0].value);
    EXPECT_EQ(index, next_index[record.thread]++);
    const auto found = last_sequence.find(record.thread);
    if (found != last_sequence.end()) {
      EXPECT_GT(record.sequence, found->second);
    }
    last_sequence[record.thread] = record.sequence;
  }
  EXPECT_EQ(next_index.size(), static_cast<std::size_t>(kThreads));
}

TEST_F(LogTest, WithoutASinkNothingIsEmitted) {
  SetSink(nullptr);
  ALUMBRA_LOG(kNet, kError) << "nowhere";
  EXPECT_TRUE(sink_->Records().empty());
}

TEST(CategoryTest, TheSharedVocabularyRoundTrips) {
  const std::vector<std::string> names = {
      "net",   "manifest", "abr", "buffer", "demux",    "decode", "render",
      "audio", "text",     "ads", "drm",    "settings", "player", "port"};
  ASSERT_EQ(names.size(), kCategoryCount);
  for (std::size_t i = 0; i < names.size(); ++i) {
    const auto category = ParseCategory(names[i]);
    ASSERT_TRUE(category.has_value()) << names[i];
    EXPECT_EQ(static_cast<std::size_t>(*category), i);
    EXPECT_EQ(CategoryName(*category), names[i]);
  }
  EXPECT_FALSE(ParseCategory("network").has_value());
  EXPECT_EQ(LevelName(Level::kWarning), "warning");
}

}
}
