#include "umbra/text/style.h"

#include <optional>

#include "gtest/gtest.h"

namespace umbra::text {
namespace {

TEST(TextStyleTest, AuthorValueSurvivesWhenNoPreferenceIsSet) {
  const StyleField<int> resolved =
      ResolveStyleField<int>(1, std::nullopt, 3, false);
  EXPECT_EQ(resolved.value, 1);
  EXPECT_FALSE(resolved.overridden);
}

TEST(TextStyleTest, PlatformPreferenceOverridesAuthorValue) {
  const StyleField<int> resolved = ResolveStyleField<int>(1, 2, 3, false);
  EXPECT_EQ(resolved.value, 2);
  EXPECT_TRUE(resolved.overridden);
}

TEST(TextStyleTest, ExplicitUserPreferenceOverridesPlatformAndAuthor) {
  const StyleField<int> resolved = ResolveStyleField<int>(1, 2, 3, true);
  EXPECT_EQ(resolved.value, 3);
  EXPECT_TRUE(resolved.overridden);
}

TEST(TextStyleTest, UnsetUserValueDoesNotOverridePlatformPreference) {
  const StyleField<int> resolved = ResolveStyleField<int>(1, 2, 3, false);
  EXPECT_EQ(resolved.value, 2);
}

}
}
