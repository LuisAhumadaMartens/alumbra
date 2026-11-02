#include <cstddef>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "harness.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/ui/input.h"
#include "umbra/ui/types.h"
#include "view_model_json.h"

namespace umbra::ui {
namespace {

struct Scenario {
  std::string name;
  UiConfig config;
  void (*run)(Harness&);
};

std::shared_ptr<const MenuNode> Menu() {
  MenuNode root;
  MenuNode quality{.id = "quality",
                   .label_key = "menu.quality",
                   .kind = MenuItemKind::kSubmenu,
                   .selected = false,
                   .children = {}};
  quality.children.push_back(MenuNode{.id = "quality.auto",
                                      .label_key = "menu.quality.auto",
                                      .kind = MenuItemKind::kChoice,
                                      .selected = true,
                                      .children = {}});
  quality.children.push_back(MenuNode{.id = "quality.1080",
                                      .label_key = "menu.quality.1080",
                                      .kind = MenuItemKind::kChoice,
                                      .selected = false,
                                      .children = {}});
  root.children.push_back(std::move(quality));
  root.children.push_back(MenuNode{.id = "captions",
                                   .label_key = "menu.captions",
                                   .kind = MenuItemKind::kToggle,
                                   .selected = false,
                                   .children = {}});
  return std::make_shared<const MenuNode>(std::move(root));
}

UiConfig WithLayout(Layout layout, bool rtl) {
  UiConfig config;
  config.layout = layout;
  config.rtl = rtl;
  return config;
}

void Idle([[maybe_unused]] Harness& harness) {}

void Loading(Harness& harness) { harness.Send(LoadStarted{}); }

void LoadingSpinner(Harness& harness) {
  harness.Send(LoadStarted{});
  harness.AdvanceMillis(500);
}

void Paused(Harness& harness) { harness.Prepare(false); }

void Playing(Harness& harness) {
  harness.Prepare(true);
  harness.Send(BufferedChanged{
      .ranges = {TimeRange{.start_us = 0, .end_us = 30'000'000}}});
  harness.AdvanceMillis(1000);
}

void PlayingHidden(Harness& harness) {
  harness.Prepare(true);
  harness.AdvanceMillis(3000);
}

void Buffering(Harness& harness) {
  harness.Prepare(true);
  harness.Send(Waiting{});
}

void BufferingSpinner(Harness& harness) {
  harness.Prepare(true);
  harness.Send(Waiting{});
  harness.AdvanceMillis(500);
}

void Ended(Harness& harness) {
  harness.Prepare(true);
  harness.AdvanceMillis(2000);
  harness.Send(PlaybackEnded{});
}

void Failed(Harness& harness) {
  harness.Prepare(true);
  harness.Send(PlaybackFailed{.code = base::ErrorCode::kNetworkTimeout});
}

void MenuOpen(Harness& harness) {
  harness.Send(SetMenu{.root = Menu()});
  harness.Prepare(true);
  harness.Send(Activate{.id = ControlId::kSettings});
}

void SubmenuOpen(Harness& harness) {
  MenuOpen(harness);
  harness.Send(Navigate{.direction = Direction::kRight});
  harness.Send(Navigate{.direction = Direction::kDown});
}

void MenuChoiceMade(Harness& harness) {
  SubmenuOpen(harness);
  harness.Send(ActivateFocused{});
}

void AdBreak(Harness& harness) {
  harness.Send(SetMenu{.root = Menu()});
  harness.Prepare(true);
  harness.Send(Activate{.id = ControlId::kSettings});
  harness.Send(AdBreakStarted{.owner = AdInputOwner::kUi, .skippable = true});
  harness.Send(AdSkipAvailable{});
}

void AdProvider(Harness& harness) {
  harness.Prepare(true);
  harness.Send(
      AdBreakStarted{.owner = AdInputOwner::kProvider, .skippable = false});
  harness.Send(TogglePlay{});
}

void Fullscreen(Harness& harness) {
  harness.Prepare(true);
  harness.Send(ToggleFullscreen{});
  harness.Send(PresentationChanged{.mode = PresentationMode::kFullscreen});
}

void PictureInPicture(Harness& harness) {
  harness.Prepare(true);
  harness.Send(
      PresentationChanged{.mode = PresentationMode::kPictureInPicture});
}

void Scrubbing(Harness& harness) {
  harness.Prepare(true);
  harness.AdvanceMillis(1000);
  harness.Send(ScrubBegin{.fraction = 0.25});
  harness.Send(ScrubMove{.fraction = 0.5});
}

void TvFocus(Harness& harness) {
  harness.Prepare(true);
  harness.Send(Navigate{.direction = Direction::kRight});
  harness.Send(Navigate{.direction = Direction::kRight});
  harness.Send(Navigate{.direction = Direction::kRight});
}

void TvSeekBarFocus(Harness& harness) {
  harness.Prepare(true);
  harness.Send(Navigate{.direction = Direction::kUp});
  harness.Send(Navigate{.direction = Direction::kUp});
  harness.Send(Navigate{.direction = Direction::kRight});
}

void Live(Harness& harness) {
  harness.Send(LoadStarted{});
  harness.Send(DurationChanged{.duration_us = 0, .live = true});
  harness.Send(SeekableChanged{.start_us = 100'000'000, .end_us = 160'000'000});
  harness.Send(CanPlay{});
  harness.Send(TogglePlay{});
  harness.Send(PlaybackStarted{});
  harness.AdvanceMillis(1000);
}

void CustomControlRegistered(Harness& harness) {
  harness.Send(RegisterControl{.name = "cast",
                               .label_key = "app.cast",
                               .icon_token = "app.icon_cast",
                               .slot = Slot::kTrailing});
  harness.Prepare(false);
  harness.Send(Activate{.id = ControlId::kFirstCustom});
  harness.Send(UpdateControl{.name = "cast",
                             .visible = std::nullopt,
                             .enabled = std::nullopt,
                             .pressed = true});
}

void RightToLeftMenu(Harness& harness) {
  harness.Send(SetMenu{.root = Menu()});
  harness.Prepare(true);
  harness.Send(Activate{.id = ControlId::kSettings});
  harness.Send(Navigate{.direction = Direction::kLeft});
}

void MutedLowVolume(Harness& harness) {
  harness.Prepare(true);
  harness.Send(SetVolume{.volume = 0.3});
  harness.Send(ToggleMute{});
}

void SeekEffects(Harness& harness) {
  harness.Prepare(true);
  harness.AdvanceMillis(10'000);
  harness.Send(SeekSteps{.steps = 2});
  harness.Send(Seeked{.position_us = 20'000'000});
  harness.Send(SeekToFraction{.fraction = 0.5});
  harness.Send(Seeked{.position_us = 300'000'000});
}

void Announcements(Harness& harness) {
  harness.Prepare(false);
  harness.Send(TogglePlay{});
  harness.Send(TogglePlay{});
  harness.Send(AdBreakStarted{.owner = AdInputOwner::kUi, .skippable = false});
}

const std::vector<Scenario>& Scenarios() {
  static const std::vector<Scenario> scenarios{
      {"idle", {}, Idle},
      {"loading", {}, Loading},
      {"loading_spinner", {}, LoadingSpinner},
      {"paused", {}, Paused},
      {"playing", {}, Playing},
      {"playing_chrome_hidden", {}, PlayingHidden},
      {"buffering", {}, Buffering},
      {"buffering_spinner", {}, BufferingSpinner},
      {"ended", {}, Ended},
      {"error", {}, Failed},
      {"menu_open", {}, MenuOpen},
      {"submenu_open", {}, SubmenuOpen},
      {"menu_choice", {}, MenuChoiceMade},
      {"menu_right_to_left", WithLayout(Layout::kDesktop, true),
       RightToLeftMenu},
      {"ad_break_skippable", {}, AdBreak},
      {"ad_break_provider", {}, AdProvider},
      {"fullscreen", {}, Fullscreen},
      {"picture_in_picture", {}, PictureInPicture},
      {"scrubbing", {}, Scrubbing},
      {"tv_focus_volume", WithLayout(Layout::kTv, false), TvFocus},
      {"tv_focus_seek_bar", WithLayout(Layout::kTv, false), TvSeekBarFocus},
      {"live", {}, Live},
      {"custom_control", {}, CustomControlRegistered},
      {"muted_low_volume", {}, MutedLowVolume},
      {"seek_effects", {}, SeekEffects},
      {"announcements", {}, Announcements},
  };
  return scenarios;
}

std::string ReadFile(const std::string& path) {
  const std::ifstream input(path, std::ios::binary);
  std::stringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

class GoldenTest : public testing::TestWithParam<std::size_t> {};

TEST_P(GoldenTest, ViewModelAndEffectsMatchTheRecordedOutput) {
  const Scenario& scenario = Scenarios()[GetParam()];
  Harness harness(scenario.config);
  scenario.run(harness);
  const std::string actual = ToJson(*harness.Snapshot(), harness.Effects());
  const std::string path =
      std::string(ALUMBRA_UI_GOLDEN_DIR) + "/" + scenario.name + ".json";
  const std::string expected = ReadFile(path);
  if (expected.empty()) {
    std::ofstream(path, std::ios::binary) << actual;
  }
  ASSERT_FALSE(expected.empty())
      << "recorded the missing golden " << path << "; review and commit it";
  EXPECT_EQ(actual, expected) << "golden " << path;
}

INSTANTIATE_TEST_SUITE_P(Scenarios, GoldenTest,
                         testing::Range<std::size_t>(0, 26),
                         [](const testing::TestParamInfo<std::size_t>& info) {
                           return Scenarios()[info.param].name;
                         });

TEST(GoldenScenarios, CoverEveryRecordedScenario) {
  EXPECT_EQ(Scenarios().size(), 26U);
}

}
}
