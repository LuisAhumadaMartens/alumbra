#include "umbra/observability/trace.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "umbra/observability/category.h"

namespace umbra::observability {
namespace {

class TraceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ClearTrace();
    SetTraceCategoryMask(0);
  }

  void TearDown() override {
    SetTraceCategoryMask(0);
    ClearTrace();
  }
};

int Evaluate(int& counter) { return ++counter; }

TEST_F(TraceTest, NothingIsRecordedWhileEveryCategoryIsDisabled) {
  int evaluations = 0;
  {
    ALUMBRA_TRACE(kNet, "disabled_scope");
    ALUMBRA_TRACE_INSTANT(kNet, "disabled_instant");
    ALUMBRA_TRACE_COUNTER(kNet, "disabled_counter", Evaluate(evaluations));
  }
  EXPECT_TRUE(DumpTrace().empty());
  EXPECT_EQ(evaluations, 0);
}

TEST_F(TraceTest, ScopesInstantsAndCountersAreRecordedInOrder) {
  SetTraceCategoryMask(CategoryBit(Category::kDecode) |
                       CategoryBit(Category::kBuffer));
  {
    ALUMBRA_TRACE(kDecode, "decode_sample");
    ALUMBRA_TRACE_COUNTER(kBuffer, "buffer_ms", 1234);
    ALUMBRA_TRACE_INSTANT(kDecode, "keyframe");
    ALUMBRA_TRACE(kNet, "other_category");
  }
  const auto events = DumpTrace();
  ASSERT_EQ(events.size(), 4U);
  EXPECT_EQ(events[0].phase, TracePhase::kBegin);
  EXPECT_STREQ(events[0].name, "decode_sample");
  EXPECT_EQ(events[0].category, Category::kDecode);
  EXPECT_EQ(events[1].phase, TracePhase::kCounter);
  EXPECT_EQ(events[1].value, 1234);
  EXPECT_EQ(events[1].category, Category::kBuffer);
  EXPECT_EQ(events[2].phase, TracePhase::kInstant);
  EXPECT_EQ(events[3].phase, TracePhase::kEnd);
  EXPECT_STREQ(events[3].name, "decode_sample");
  for (std::size_t i = 1; i < events.size(); ++i) {
    EXPECT_LE(events[i - 1].timestamp_ns, events[i].timestamp_ns);
    EXPECT_EQ(events[i].thread, events[0].thread);
  }
}

TEST_F(TraceTest, CounterValueIsOnlyEvaluatedForEnabledCategories) {
  int evaluations = 0;
  ALUMBRA_TRACE_COUNTER(kAudio, "samples", Evaluate(evaluations));
  EXPECT_EQ(evaluations, 0);
  SetTraceCategoryMask(CategoryBit(Category::kAudio));
  ALUMBRA_TRACE_COUNTER(kAudio, "samples", Evaluate(evaluations));
  EXPECT_EQ(evaluations, 1);
  EXPECT_EQ(DumpTrace().back().value, 1);
}

TEST_F(TraceTest, TheRingIsBoundedAndKeepsTheNewestEvents) {
  SetTraceCategoryMask(CategoryBit(Category::kRender));
  const auto total = static_cast<std::int64_t>(kTraceRingCapacity) + 100;
  for (std::int64_t i = 0; i < total; ++i) {
    ALUMBRA_TRACE_COUNTER(kRender, "frame", i);
  }
  const auto events = DumpTrace();
  ASSERT_EQ(events.size(), kTraceRingCapacity);
  EXPECT_EQ(events.front().value,
            total - static_cast<std::int64_t>(kTraceRingCapacity));
  EXPECT_EQ(events.back().value, total - 1);
}

TEST_F(TraceTest, ConcurrentWritersAndDumpsStayConsistent) {
  SetTraceCategoryMask(kAllCategories);
  std::vector<std::thread> threads;
  threads.reserve(4);
  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([] {
      for (int i = 0; i < 5000; ++i) {
        ALUMBRA_TRACE(kPlayer, "writer_scope");
        ALUMBRA_TRACE_COUNTER(kPlayer, "writer_counter", i);
      }
    });
  }
  for (int i = 0; i < 20; ++i) {
    for (const TraceEvent& event : DumpTrace()) {
      ASSERT_NE(event.name, nullptr);
      EXPECT_TRUE(std::strcmp(event.name, "writer_scope") == 0 ||
                  std::strcmp(event.name, "writer_counter") == 0);
    }
  }
  for (std::thread& thread : threads) {
    thread.join();
  }
  EXPECT_EQ(DumpTrace().size(), kTraceRingCapacity);
}

TEST_F(TraceTest, DumpOnDemandFormatsReadableLines) {
  SetTraceCategoryMask(CategoryBit(Category::kAbr));
  ALUMBRA_TRACE_COUNTER(kAbr, "throughput_bps", 5000);
  ALUMBRA_TRACE_INSTANT(kAbr, "switch");
  const std::string text = FormatTrace(DumpTrace());
  EXPECT_NE(text.find(" C abr throughput_bps 5000\n"), std::string::npos);
  EXPECT_NE(text.find(" I abr switch\n"), std::string::npos);
  ClearTrace();
  EXPECT_TRUE(DumpTrace().empty());
}

}
}
