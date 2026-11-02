#undef ALUMBRA_TRACE_ENABLED
#define ALUMBRA_TRACE_ENABLED 0
#undef ALUMBRA_LOG_FLOOR_LEVEL
#define ALUMBRA_LOG_FLOOR_LEVEL 5

#include <memory>

#include "capturing_sink.h"
#include "gtest/gtest.h"
#include "umbra/observability/category.h"
#include "umbra/observability/log.h"
#include "umbra/observability/trace.h"

namespace umbra::observability {
namespace {

struct EvaluationCounter {
  mutable int calls = 0;
  int operator()() const { return ++calls; }
};

TEST(CompiledOutTest, TraceMacroArgumentsAreNeverEvaluated) {
  SetTraceCategoryMask(kAllCategories);
  ClearTrace();
  const EvaluationCounter evaluate{};
  {
    ALUMBRA_TRACE(kNet, "compiled_out_scope");
    ALUMBRA_TRACE_INSTANT(kNet, "compiled_out_instant");
    ALUMBRA_TRACE_COUNTER(kNet, "compiled_out_counter", evaluate());
  }
  EXPECT_EQ(evaluate.calls, 0);
  EXPECT_TRUE(DumpTrace().empty());
  SetTraceCategoryMask(0);
}

TEST(CompiledOutTest, LogMacroArgumentsAreNeverEvaluatedAtAnyRuntimeSetting) {
  auto sink = std::make_shared<CapturingSink>();
  SetSink(sink);
  SetLogLevel(Level::kVerbose);
  SetLogCategoryMask(kAllCategories);
  const EvaluationCounter evaluate{};
  ALUMBRA_LOG(kNet, kError).Field("value", evaluate()) << evaluate();
  ALUMBRA_LOG_EVERY_N(kNet, kError, 1) << evaluate();
  ALUMBRA_LOG_EVERY_MS(kNet, kError, 0) << evaluate();
  EXPECT_EQ(evaluate.calls, 0);
  EXPECT_TRUE(sink->Records().empty());
  SetSink(nullptr);
  SetLogLevel(Level::kInfo);
}

}
}
