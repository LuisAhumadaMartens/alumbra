#undef ALUMBRA_LOG_FLOOR_LEVEL
#define ALUMBRA_LOG_FLOOR_LEVEL 3

#include <memory>

#include "capturing_sink.h"
#include "gtest/gtest.h"
#include "umbra/observability/category.h"
#include "umbra/observability/log.h"

namespace umbra::observability {
namespace {

int Evaluate(int& counter) { return ++counter; }

TEST(LogFloorTest, RuntimeSettingsCannotEnableSeveritiesBelowTheFloor) {
  auto sink = std::make_shared<CapturingSink>();
  SetSink(sink);
  SetLogCategoryMask(kAllCategories);
  SetLogLevel(Level::kVerbose);
  int evaluations = 0;
  ALUMBRA_LOG(kNet, kVerbose) << Evaluate(evaluations);
  ALUMBRA_LOG(kNet, kDebug) << Evaluate(evaluations);
  ALUMBRA_LOG(kNet, kInfo) << Evaluate(evaluations);
  EXPECT_EQ(evaluations, 0);
  EXPECT_TRUE(sink->Records().empty());
  ALUMBRA_LOG(kNet, kWarning) << "warning";
  ALUMBRA_LOG(kNet, kError) << "error";
  EXPECT_EQ(sink->Records().size(), 2U);
  SetLogLevel(Level::kError);
  ALUMBRA_LOG(kNet, kWarning) << "restricted further";
  EXPECT_EQ(sink->Records().size(), 2U);
  SetSink(nullptr);
  SetLogLevel(Level::kInfo);
}

}
}
