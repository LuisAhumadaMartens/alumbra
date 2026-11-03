#include "umbra/ads/obstruction_registry.h"

#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"

namespace umbra::ads {
namespace {

class MockProvider final : public ObstructionProvider {
 public:
  absl::Status RegisterFriendlyObstruction(
      const ObstructionEntry& entry) override {
    registered.push_back(entry.view);
    return absl::OkStatus();
  }

  absl::Status UnregisterFriendlyObstruction(ViewHandle view) override {
    unregistered.push_back(view);
    return absl::OkStatus();
  }

  std::vector<ViewHandle> registered;
  std::vector<ViewHandle> unregistered;
};

ObstructionEntry Entry(ViewHandle view, ObstructionPurpose purpose,
                       Rectangle bounds) {
  return ObstructionEntry{.view = view,
                          .role = ViewRole::kFriendlyObstruction,
                          .purpose = purpose,
                          .reason = "Transient controls",
                          .bounds = bounds};
}

TEST(ObstructionRegistryTest, RejectsReasonOutsideTheOMIDCharacterRules) {
  ObstructionRegistry registry(ProviderMode::kRecordOnly, nullptr);
  ObstructionEntry entry =
      Entry(1, ObstructionPurpose::kMediaControls,
            Rectangle{.x = 0, .y = 0, .width = 10, .height = 10});
  entry.reason = "Controls!";

  const absl::Status status = registry.DeclareView(std::move(entry));

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(status.message().find("1 to 50 letters, digits, or spaces"),
            std::string::npos);
}

TEST(ObstructionRegistryTest,
     ForwardModeRegistersAndUnregistersEachEntryPerBreak) {
  MockProvider provider;
  ObstructionRegistry registry(ProviderMode::kForward, &provider);
  ASSERT_TRUE(registry
                  .DeclareView(Entry(
                      1, ObstructionPurpose::kMediaControls,
                      Rectangle{.x = 0, .y = 70, .width = 100, .height = 20}))
                  .ok());
  ASSERT_TRUE(registry
                  .DeclareView(Entry(
                      2, ObstructionPurpose::kCloseAd,
                      Rectangle{.x = 0, .y = 0, .width = 20, .height = 20}))
                  .ok());
  ASSERT_TRUE(
      registry
          .DeclareView(ObstructionEntry{
              .view = 3,
              .role = ViewRole::kNeverShownOverAds,
              .purpose = ObstructionPurpose::kUnspecified,
              .reason = "Brand mark",
              .bounds =
                  Rectangle{.x = 200, .y = 200, .width = 10, .height = 10}})
          .ok());

  const Rectangle ad_container{.x = 0, .y = 0, .width = 100, .height = 70};
  ASSERT_TRUE(registry.BeginBreak(ad_container).ok());
  ASSERT_TRUE(registry.EndBreak().ok());
  ASSERT_TRUE(registry.BeginBreak(ad_container).ok());
  ASSERT_TRUE(registry.EndBreak().ok());

  EXPECT_EQ(provider.registered, (std::vector<ViewHandle>{1, 2, 1, 2}));
  EXPECT_EQ(provider.unregistered, (std::vector<ViewHandle>{1, 2, 1, 2}));
}

TEST(ObstructionRegistryTest, AdModeRejectsViewsWithoutADeclaredRole) {
  ObstructionRegistry registry(ProviderMode::kRecordOnly, nullptr);
  ASSERT_TRUE(
      registry
          .DeclareView(ObstructionEntry{
              .view = 1,
              .role = ViewRole::kUnspecified,
              .purpose = ObstructionPurpose::kMediaControls,
              .reason = "Transient controls",
              .bounds = Rectangle{.x = 0, .y = 70, .width = 100, .height = 20}})
          .ok());

  const absl::Status status = registry.BeginBreak(
      Rectangle{.x = 0, .y = 0, .width = 100, .height = 70});

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(status.message().find("declared role"), std::string::npos);
  EXPECT_FALSE(registry.BreakActive());
}

TEST(ObstructionRegistryTest, EnforceModeRejectsAnOverlappingRegisteredView) {
  ObstructionRegistry registry(ProviderMode::kEnforceNoOverlap, nullptr);
  ASSERT_TRUE(registry
                  .DeclareView(Entry(
                      1, ObstructionPurpose::kMediaControls,
                      Rectangle{.x = 90, .y = 20, .width = 20, .height = 20}))
                  .ok());

  const absl::Status status = registry.BeginBreak(
      Rectangle{.x = 0, .y = 0, .width = 100, .height = 100});

  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_NE(status.message().find("overlaps the ad container"),
            std::string::npos);
  EXPECT_FALSE(registry.BreakActive());
}

TEST(ObstructionRegistryTest, DebugRegionsIncludeFriendlyAndForbiddenAreas) {
  ObstructionRegistry registry(ProviderMode::kRecordOnly, nullptr);
  ASSERT_TRUE(registry
                  .DeclareView(Entry(
                      1, ObstructionPurpose::kMediaControls,
                      Rectangle{.x = 0, .y = 70, .width = 100, .height = 20}))
                  .ok());
  ASSERT_TRUE(
      registry.BeginBreak(Rectangle{.x = 0, .y = 0, .width = 100, .height = 70})
          .ok());

  const std::vector<DebugRegion> regions = registry.DebugRegions();

  ASSERT_EQ(regions.size(), 2);
  EXPECT_FALSE(regions[0].forbidden);
  EXPECT_TRUE(regions[1].forbidden);
  EXPECT_EQ(regions[1].bounds,
            (Rectangle{.x = 0, .y = 0, .width = 100, .height = 70}));
  EXPECT_TRUE(registry.EndBreak().ok());
}

}
}
