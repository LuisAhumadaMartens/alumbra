#include "umbra/observability/settings_binding.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "capturing_sink.h"
#include "gtest/gtest.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/test_support/test_task_environment.h"
#include "umbra/observability/category.h"
#include "umbra/observability/log.h"
#include "umbra/observability/trace.h"
#include "umbra/settings/platform.h"
#include "umbra/settings/settings_keys.h"
#include "umbra/settings/store.h"

namespace umbra::observability {
namespace {

class SettingsBindingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    SetSink(sink_);
    ClearTrace();
    auto binding = SettingsBinding::Create(store_, runner_);
    ASSERT_TRUE(binding.ok());
    binding_ = *std::move(binding);
  }

  void TearDown() override {
    binding_.reset();
    SetSink(nullptr);
    SetLogLevel(Level::kInfo);
    SetLogCategoryMask(kAllCategories);
    SetTraceCategoryMask(0);
    ClearTrace();
  }

  void Apply() { environment_.RunUntilIdle(); }

  settings::Store store_{settings::GetRegistry(), settings::Platform::kLinux};
  base::TestTaskEnvironment environment_;
  std::shared_ptr<base::SequencedTaskRunner> runner_ =
      environment_.CreateSequencedTaskRunner(base::TaskPriority::kControl);
  std::shared_ptr<CapturingSink> sink_ = std::make_shared<CapturingSink>();
  std::unique_ptr<SettingsBinding> binding_;
};

TEST_F(SettingsBindingTest, SchemaDefaultsApplyImmediately) {
  EXPECT_EQ(GetLogLevel(), Level::kInfo);
  EXPECT_EQ(GetLogCategoryMask(), kAllCategories);
  EXPECT_EQ(GetTraceCategoryMask(), 0U);
}

TEST_F(SettingsBindingTest, LogLevelFollowsTheSetting) {
  ALUMBRA_LOG(kNet, kWarning) << "before";
  EXPECT_EQ(sink_->Records().size(), 1U);
  ASSERT_TRUE(store_
                  .Set(settings::Layer::kUser, settings::kObservabilityLogLevel,
                       settings::ObservabilityLogLevel::kError)
                  .ok());
  Apply();
  EXPECT_EQ(GetLogLevel(), Level::kError);
  ALUMBRA_LOG(kNet, kWarning) << "filtered";
  ALUMBRA_LOG(kNet, kError) << "kept";
  EXPECT_EQ(sink_->Records().size(), 2U);
  store_.Reset(settings::Layer::kUser);
  Apply();
  EXPECT_EQ(GetLogLevel(), Level::kInfo);
}

TEST_F(SettingsBindingTest, LogCategoriesFollowTheSetting) {
  ASSERT_TRUE(store_
                  .Set(settings::Layer::kUser,
                       settings::kObservabilityLogCategories,
                       std::vector<std::string>{"net", "drm"})
                  .ok());
  Apply();
  EXPECT_EQ(GetLogCategoryMask(),
            CategoryBit(Category::kNet) | CategoryBit(Category::kDrm));
  ALUMBRA_LOG(kNet, kError) << "net";
  ALUMBRA_LOG(kAbr, kError) << "abr";
  ALUMBRA_LOG(kDrm, kError) << "drm";
  const auto records = sink_->Records();
  ASSERT_EQ(records.size(), 2U);
  EXPECT_EQ(records[0].message, "net");
  EXPECT_EQ(records[1].message, "drm");
  store_.Reset(settings::Layer::kUser);
  Apply();
  EXPECT_EQ(GetLogCategoryMask(), kAllCategories);
}

TEST_F(SettingsBindingTest, TraceCategoriesEnableAndDisableTracing) {
  {
    ALUMBRA_TRACE(kDecode, "scope_before");
  }
  EXPECT_TRUE(DumpTrace().empty());
  ASSERT_TRUE(store_
                  .Set(settings::Layer::kUser,
                       settings::kObservabilityTraceCategories,
                       std::vector<std::string>{"decode"})
                  .ok());
  Apply();
  EXPECT_EQ(GetTraceCategoryMask(), CategoryBit(Category::kDecode));
  {
    ALUMBRA_TRACE(kDecode, "scope_after");
    ALUMBRA_TRACE(kNet, "scope_other");
  }
  EXPECT_EQ(DumpTrace().size(), 2U);
  store_.Reset(settings::Layer::kUser);
  Apply();
  EXPECT_EQ(GetTraceCategoryMask(), 0U);
}

}
}
