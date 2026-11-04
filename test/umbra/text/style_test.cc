#include "umbra/text/style.h"

#include <cstdint>
#include <optional>

#include "gtest/gtest.h"
#include "umbra/settings/platform.h"
#include "umbra/settings/settings_keys.h"
#include "umbra/settings/store.h"

namespace umbra::text {
namespace {

TEST(TextStyleTest, AuthorValueSurvivesWhenNoPreferenceIsSet) {
  settings::Store store(settings::GetRegistry(), settings::Platform::kLinux);
  const settings::Snapshot preferences = store.TakeSnapshot();
  const StyleField<std::int64_t> resolved = ResolveStyleField<std::int64_t>(
      100, std::nullopt, preferences, settings::kTextFontSizePercent);
  EXPECT_EQ(resolved.value, 100);
  EXPECT_FALSE(resolved.overridden);
}

TEST(TextStyleTest, PlatformPreferenceOverridesAuthorValue) {
  settings::Store store(settings::GetRegistry(), settings::Platform::kLinux);
  const settings::Snapshot preferences = store.TakeSnapshot();
  const StyleField<std::int64_t> resolved = ResolveStyleField<std::int64_t>(
      100, 120, preferences, settings::kTextFontSizePercent);
  EXPECT_EQ(resolved.value, 120);
  EXPECT_TRUE(resolved.overridden);
}

TEST(TextStyleTest, ExplicitUserPreferenceOverridesPlatformAndAuthor) {
  settings::Store store(settings::GetRegistry(), settings::Platform::kLinux);
  ASSERT_TRUE(store
                  .Set(settings::Layer::kUser, settings::kTextFontSizePercent,
                       std::int64_t{130})
                  .ok());
  const settings::Snapshot preferences = store.TakeSnapshot();
  const StyleField<std::int64_t> resolved = ResolveStyleField<std::int64_t>(
      100, 120, preferences, settings::kTextFontSizePercent);
  EXPECT_EQ(resolved.value, 130);
  EXPECT_TRUE(resolved.overridden);
}

TEST(TextStyleTest, UnsetUserValueDoesNotOverridePlatformPreference) {
  settings::Store store(settings::GetRegistry(), settings::Platform::kLinux);
  const settings::Snapshot preferences = store.TakeSnapshot();
  const StyleField<std::int64_t> resolved = ResolveStyleField<std::int64_t>(
      100, 120, preferences, settings::kTextFontSizePercent);
  EXPECT_EQ(resolved.value, 120);
  EXPECT_TRUE(resolved.overridden);
}

}
}
