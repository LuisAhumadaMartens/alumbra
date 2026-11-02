#include <algorithm>
#include <cstddef>
#include <string_view>

#include "gtest/gtest.h"
#include "umbra/settings/setting_types.h"
#include "umbra/settings/setting_value.h"
#include "umbra/settings/settings_keys.h"
#include "umbra/settings/testing/all_types_keys.h"

namespace umbra::settings {
namespace {

void ExpectWellFormed(const Registry& registry) {
  EXPECT_TRUE(
      std::ranges::is_sorted(registry.settings, {}, &SettingInfo::name));
  EXPECT_TRUE(std::ranges::is_sorted(registry.aliases, {}, &AliasInfo::name));
  for (std::size_t index = 0; index < registry.settings.size(); ++index) {
    const SettingInfo& info = registry.settings[index];
    const auto found = FindSetting(registry, info.name);
    ASSERT_TRUE(found.has_value()) << info.name;
    EXPECT_EQ(found->index, index);
    EXPECT_FALSE(found->via_alias);
    EXPECT_FALSE(FindViolation(info, DefaultValue(info)).has_value())
        << info.name;
  }
  for (const AliasInfo& alias : registry.aliases) {
    const auto found = FindSetting(registry, alias.name);
    ASSERT_TRUE(found.has_value()) << alias.name;
    EXPECT_EQ(found->index, alias.index);
    EXPECT_TRUE(found->via_alias);
  }
  EXPECT_FALSE(FindSetting(registry, "missing.key").has_value());
  EXPECT_FALSE(FindSetting(registry, "").has_value());
}

TEST(RegistryTest, ProductionRegistryIsSortedAndSelfConsistent) {
  ExpectWellFormed(GetRegistry());
}

TEST(RegistryTest, FixtureRegistryIsSortedAndSelfConsistent) {
  ExpectWellFormed(all_types::GetRegistry());
}

TEST(RegistryTest, KeyConstantsIndexTheirOwnRecords) {
  const Registry registry = GetRegistry();
  EXPECT_EQ(registry.settings[kNetworkRetryMaxAttempts.Index()].name,
            "network.retry.max_attempts");
  EXPECT_EQ(registry.settings[kObservabilityLogLevel.Index()].name,
            "observability.log.level");
  EXPECT_EQ(registry.settings[kEngineThreadsYieldBudgetMs.Index()].name,
            "engine.threads.yield_budget_ms");
  const Registry fixture = all_types::GetRegistry();
  EXPECT_EQ(fixture.settings[all_types::kTestTimeoutMs.Index()].name,
            "test.timeout_ms");
  EXPECT_EQ(fixture.settings[all_types::kTestCapacityBytes.Index()].name,
            "test.capacity_bytes");
}

TEST(RegistryTest, FixtureCarriesAliasAndDeprecationMetadata) {
  const Registry fixture = all_types::GetRegistry();
  const auto alias = FindSetting(fixture, "test.old_timeout_ms");
  ASSERT_TRUE(alias.has_value());
  EXPECT_EQ(alias->index, all_types::kTestTimeoutMs.Index());
  EXPECT_TRUE(
      fixture.settings[all_types::kTestCapacityBytes.Index()].deprecated);
  EXPECT_FALSE(fixture.settings[all_types::kTestCount.Index()].deprecated);
}

}
}
