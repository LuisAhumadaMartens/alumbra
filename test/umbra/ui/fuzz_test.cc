#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "gtest/gtest.h"
#include "harness.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/ui/input.h"
#include "umbra/ui/reduce.h"
#include "umbra/ui/types.h"
#include "umbra/ui/view_model.h"
#include "view_model_json.h"

namespace umbra::ui {
namespace {

constexpr std::uint64_t kFixedSeeds = 16;
constexpr int kStepsPerSeed = 2500;

class Rng {
 public:
  explicit Rng(std::uint64_t seed) : state_(seed) {}

  std::uint64_t Next() {
    state_ += 0x9E3779B97F4A7C15ULL;
    std::uint64_t value = state_;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31);
  }
  std::uint64_t Below(std::uint64_t bound) { return Next() % bound; }
  double Unit() {
    return static_cast<double>(Next() >> 11) / 9007199254740992.0;
  }
  double Fraction() { return (Unit() * 1.4) - 0.2; }
  int Steps() { return static_cast<int>(Below(7)) - 3; }

 private:
  std::uint64_t state_;
};

ControlId RandomControl(Rng& rng) {
  constexpr std::array<std::uint16_t, 11> kIds{1, 2,  3,  4,  5,  6,
                                               7, 64, 65, 66, 200};
  return static_cast<ControlId>(kIds.at(rng.Below(kIds.size())));
}

Direction RandomDirection(Rng& rng) {
  return static_cast<Direction>(rng.Below(4));
}

Intent RandomIntent(Rng& rng) {
  switch (rng.Below(34)) {
    case 0:
      return TogglePlay{};
    case 1:
      return Play{};
    case 2:
      return Pause{};
    case 3:
      return SeekSteps{.steps = rng.Steps()};
    case 4:
      return SeekToFraction{.fraction = rng.Fraction()};
    case 5:
      return ScrubBegin{.fraction = rng.Fraction()};
    case 6:
      return ScrubMove{.fraction = rng.Fraction()};
    case 7:
      return ScrubEnd{};
    case 8:
      return ScrubCancel{};
    case 9:
      return SetVolume{.volume = rng.Fraction()};
    case 10:
      return VolumeSteps{.steps = rng.Steps()};
    case 11:
      return ToggleMute{};
    case 12:
      return ToggleFullscreen{};
    case 13:
      return ShowControls{};
    case 14:
      return HideControls{};
    case 15:
      return ToggleControls{};
    case 16:
      return PointerActivity{};
    case 17:
      return PointerEntered{};
    case 18:
      return PointerLeft{};
    case 19:
      return Navigate{.direction = RandomDirection(rng)};
    case 20:
      return Back{};
    case 21:
      return ActivateFocused{};
    case 22:
      return Activate{.id = RandomControl(rng)};
    case 23:
      return FocusControl{.id = RandomControl(rng)};
    case 24:
      return FocusTraverse{.backward = rng.Below(2) == 0};
    case 25:
      return FocusChrome{};
    case 26:
      return MoveFocus{.direction = RandomDirection(rng)};
    case 27:
      return OpenMenu{};
    case 28:
      return MenuNavigate{.direction = RandomDirection(rng)};
    case 29:
      return MenuBack{};
    case 30:
      return MenuActivate{};
    case 31:
      return SkipAd{};
    case 32:
      return ActivateCustom{.id = RandomControl(rng)};
    default:
      return PassBack{};
  }
}

PresentationMode RandomPresentation(Rng& rng) {
  return static_cast<PresentationMode>(rng.Below(3));
}

EngineEvent RandomEvent(Rng& rng) {
  const auto position = static_cast<Micros>(rng.Below(700'000'000));
  switch (rng.Below(21)) {
    case 0:
      return LoadStarted{};
    case 1:
      return SourceCleared{};
    case 2:
      return CanPlay{};
    case 3:
      return PlaybackStarted{};
    case 4:
      return PlaybackPaused{};
    case 5:
      return Waiting{};
    case 6:
      return Seeking{};
    case 7:
      return Seeked{.position_us = position};
    case 8:
      return TimeUpdate{.position_us = position,
                        .anchor_us = static_cast<Micros>(rng.Below(2))};
    case 9:
      return DurationChanged{
          .duration_us = static_cast<Micros>(rng.Below(600'000'000)),
          .live = rng.Below(4) == 0};
    case 10:
      return SeekableChanged{.start_us = position / 2, .end_us = position};
    case 11:
      return BufferedChanged{
          .ranges = {TimeRange{.start_us = 0, .end_us = position}}};
    case 12:
      return VolumeChanged{.volume = rng.Fraction(),
                           .muted = rng.Below(2) == 0};
    case 13:
      return RateChanged{.rate = rng.Unit() * 3,
                         .resample_ratio = 0.9 + (rng.Unit() * 0.2)};
    case 14:
      return PlaybackEnded{};
    case 15:
      return PlaybackFailed{.code = base::ErrorCode::kMediaCorrupt};
    case 16:
      return PlayRejected{};
    case 17:
      return PresentationChanged{.mode = RandomPresentation(rng)};
    case 18:
      return AdBreakStarted{.owner = rng.Below(3) == 0 ? AdInputOwner::kProvider
                                                       : AdInputOwner::kUi,
                            .skippable = rng.Below(2) == 0};
    case 19:
      return AdSkipAvailable{};
    default:
      return AdBreakEnded{};
  }
}

std::shared_ptr<const MenuNode> RandomMenu(Rng& rng) {
  if (rng.Below(4) == 0) {
    return nullptr;
  }
  MenuNode root;
  const std::uint64_t count = rng.Below(4);
  for (std::uint64_t i = 0; i < count; ++i) {
    MenuNode item{.id = "item" + std::to_string(i),
                  .label_key = "menu.item",
                  .kind = static_cast<MenuItemKind>(rng.Below(3)),
                  .selected = false,
                  .children = {}};
    if (item.kind == MenuItemKind::kSubmenu) {
      item.children.push_back(MenuNode{.id = "child",
                                       .label_key = "menu.child",
                                       .kind = MenuItemKind::kChoice,
                                       .selected = false,
                                       .children = {}});
    }
    root.children.push_back(std::move(item));
  }
  return std::make_shared<const MenuNode>(std::move(root));
}

CatalogChange RandomCatalog(Rng& rng) {
  const std::string name = "custom" + std::to_string(rng.Below(3));
  switch (rng.Below(3)) {
    case 0:
      return RegisterControl{.name = name,
                             .label_key = "app.custom",
                             .icon_token = "app.icon",
                             .slot = static_cast<Slot>(1 + rng.Below(2))};
    case 1:
      return UpdateControl{
          .name = name,
          .visible = rng.Below(2) == 0 ? std::optional<bool>(rng.Below(2) == 0)
                                       : std::nullopt,
          .enabled = rng.Below(2) == 0 ? std::optional<bool>(rng.Below(2) == 0)
                                       : std::nullopt,
          .pressed = std::nullopt};
    default:
      return SetMenu{.root = RandomMenu(rng)};
  }
}

Input RandomInput(Rng& rng) {
  const std::uint64_t pick = rng.Below(100);
  if (pick < 60) {
    return RandomIntent(rng);
  }
  if (pick < 85) {
    return RandomEvent(rng);
  }
  if (pick < 92) {
    return RandomCatalog(rng);
  }
  return TimerFired{.kind = static_cast<TimerKind>(rng.Below(3)),
                    .generation = rng.Below(40)};
}

std::string Describe(const Input& input) {
  std::string text = std::to_string(input.index());
  if (const auto* intent = std::get_if<Intent>(&input)) {
    text += "." + std::to_string(intent->index());
  } else if (const auto* event = std::get_if<EngineEvent>(&input)) {
    text += "." + std::to_string(event->index());
  } else if (const auto* change = std::get_if<CatalogChange>(&input)) {
    text += "." + std::to_string(change->index());
  }
  return text;
}

bool HiddenControlWasActivated(const Harness& harness, const Input& input,
                               std::size_t effects_before) {
  const auto* intent = std::get_if<Intent>(&input);
  if (intent == nullptr) {
    return false;
  }
  std::optional<ControlId> target;
  if (const auto* activate = std::get_if<Activate>(intent)) {
    target = activate->id;
  } else if (const auto* custom = std::get_if<ActivateCustom>(intent)) {
    target = custom->id;
  }
  return target.has_value() && harness.Effects().size() != effects_before;
}

struct FuzzRun {
  std::string failure;
  std::string final_json;
};

FuzzRun RunSeed(std::uint64_t seed, int steps) {
  Rng rng(seed);
  UiConfig config;
  config.layout = static_cast<Layout>(rng.Below(3));
  config.rtl = rng.Below(2) == 0;
  Harness harness(config);
  std::vector<std::string> trace;
  for (int step = 0; step < steps; ++step) {
    std::string description;
    if (rng.Below(4) == 0) {
      constexpr std::array<int, 8> kAdvances{0,   1,    50,   499,
                                             500, 2999, 3000, 5000};
      const int millis = kAdvances.at(rng.Below(kAdvances.size()));
      description = "advance " + std::to_string(millis);
      harness.AdvanceMillis(millis);
    } else {
      const Input input = RandomInput(rng);
      description = Describe(input);
      const std::vector<ControlView> controls =
          DeriveControls(harness.State(), config);
      const std::size_t effects_before = harness.Effects().size();
      std::optional<bool> activatable;
      if (const auto* intent = std::get_if<Intent>(&input)) {
        if (const auto* activate = std::get_if<Activate>(intent)) {
          const ControlView* control = FindControl(controls, activate->id);
          activatable =
              control != nullptr && control->visible && control->enabled;
        }
      }
      harness.Send(input);
      if (activatable == std::optional<bool>(false) &&
          HiddenControlWasActivated(harness, input, effects_before)) {
        return {.failure = "a hidden or disabled control was activated",
                .final_json = {}};
      }
    }
    trace.push_back(description);
    const std::vector<std::string> violations =
        CheckInvariants(harness.State(), config);
    if (!violations.empty()) {
      std::string report = violations.front() + "; sequence:";
      for (std::size_t i = trace.size() > 12 ? trace.size() - 12 : 0;
           i < trace.size(); ++i) {
        report += " [" + trace[i] + "]";
      }
      return {.failure = report, .final_json = {}};
    }
  }
  return {.failure = {},
          .final_json = ToJson(*harness.Snapshot(), harness.Effects())};
}

TEST(IntentFuzz, InvariantsHoldAfterEveryStepForTheFixedSeeds) {
  for (std::uint64_t seed = 1; seed <= kFixedSeeds; ++seed) {
    const FuzzRun run = RunSeed(seed, kStepsPerSeed);
    EXPECT_TRUE(run.failure.empty())
        << "seed " << seed << " of " << kStepsPerSeed
        << " steps failed: " << run.failure;
  }
}

TEST(IntentFuzz, ASeedReplaysToTheSameResult) {
  const FuzzRun first = RunSeed(7, 800);
  const FuzzRun second = RunSeed(7, 800);
  EXPECT_TRUE(first.failure.empty()) << first.failure;
  EXPECT_EQ(first.final_json, second.final_json);
  EXPECT_NE(first.final_json, RunSeed(8, 800).final_json);
}

}
}
