#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_stdinc.h>
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/statusor.h"
#include "alumbra/design_tokens/design_tokens.h"
#include "alumbra/desktop_ui/layout.h"
#include "alumbra/desktop_ui/strings.h"
#include "golden_image.h"
#include "harness.h"
#include "penumbra/desktop/video/presenter.h"
#include "scenarios.h"
#include "ui_rig.h"
#include "umbra/ui/effect.h"
#include "umbra/ui/types.h"

namespace alumbra::desktop_ui {
namespace {

using penumbra::desktop::RgbaImage;
using testing::Rig;
using testing::RigOptions;
using testing::UiGpuTest;

constexpr int kPerPixelThreshold = 24;
constexpr double kExceedingFractionLimit = 0.004;
constexpr double kMeanLimit = 0.5;

void ExpectGolden(const std::string& name, const RgbaImage& actual) {
  const std::string golden_path =
      std::string(ALUMBRA_UI_GOLDEN_DIR) + "/" + name + ".ppm";
  const std::string actual_path =
      std::string(ALUMBRA_UI_ACTUAL_DIR) + "/" + name + ".ppm";
  const char* update = SDL_getenv("ALUMBRA_UI_GOLDEN_UPDATE");
  if (update != nullptr && std::string(update) == "1") {
    ASSERT_TRUE(testing::WriteFile(golden_path, testing::ToPpm(actual)));
    return;
  }
  const std::optional<std::string> bytes = testing::ReadFile(golden_path);
  if (!bytes.has_value()) {
    testing::WriteFile(actual_path, testing::ToPpm(actual));
    FAIL() << "Missing golden " << golden_path << "; review " << actual_path
           << " and commit it with ALUMBRA_UI_GOLDEN_UPDATE=1";
  }
  const std::optional<RgbaImage> expected = testing::FromPpm(*bytes);
  ASSERT_TRUE(expected.has_value()) << golden_path;
  ASSERT_EQ(actual.size, expected->size);
  const testing::Comparison result =
      testing::Compare(actual, *expected, kPerPixelThreshold);
  const bool within = result.exceeding_fraction <= kExceedingFractionLimit &&
                      result.mean_difference <= kMeanLimit;
  if (!within) {
    testing::WriteFile(actual_path, testing::ToPpm(actual));
  }
  EXPECT_TRUE(within) << name << ": max " << result.max_difference << ", mean "
                      << result.mean_difference << ", exceeding "
                      << result.exceeding_fraction << "; actual image in "
                      << actual_path;
}

RigOptions Options() { return RigOptions{}; }

std::vector<std::uint8_t> Pixel(const RgbaImage& image, int x, int y) {
  const std::size_t offset = ((static_cast<std::size_t>(y) *
                               static_cast<std::size_t>(image.size.width)) +
                              static_cast<std::size_t>(x)) *
                             4;
  return {image.pixels.at(offset), image.pixels.at(offset + 1),
          image.pixels.at(offset + 2)};
}

std::uint8_t Channel(double value) {
  return static_cast<std::uint8_t>(std::lround(value * 255.0));
}

class DesktopUiGoldenTest : public UiGpuTest {
 protected:
  static void RunGolden(const std::string& name,
                        void (*scenario)(umbra::ui::Harness&),
                        const RigOptions& options = Options()) {
    std::unique_ptr<Rig> rig = MakeRig(options);
    if (rig == nullptr) {
      return;
    }
    scenario(rig->harness);
    ExpectGolden(name, rig->Render());
  }
};

TEST_F(DesktopUiGoldenTest, PlayingWithControls) {
  RunGolden("playing", testing::Playing);
}

TEST_F(DesktopUiGoldenTest, Paused) { RunGolden("paused", testing::Paused); }

TEST_F(DesktopUiGoldenTest, Buffering) {
  RunGolden("buffering", testing::Buffering);
}

TEST_F(DesktopUiGoldenTest, Error) { RunGolden("error", testing::Failed); }

TEST_F(DesktopUiGoldenTest, ControlsHidden) {
  RunGolden("hidden", testing::ControlsHidden);
}

TEST_F(DesktopUiGoldenTest, FocusRing) { RunGolden("focus", testing::Focused); }

TEST_F(DesktopUiGoldenTest, HighContrast) {
  RigOptions options;
  options.ui.context.contrast = design_tokens::Contrast::kHigh;
  RunGolden("high_contrast", testing::Focused, options);
}

TEST_F(DesktopUiGoldenTest, PseudoAccentedError) {
  RigOptions options;
  options.ui.locale = Locale::kPseudoAccented;
  RunGolden("error_accented", testing::Failed, options);
}

TEST_F(DesktopUiGoldenTest, PseudoMirroredError) {
  RigOptions options;
  options.ui.locale = Locale::kPseudoMirrored;
  options.config.rtl = true;
  RunGolden("error_mirrored", testing::Failed, options);
}

TEST_F(DesktopUiGoldenTest, TokenOverrideChangesTheImageWithoutCodeChanges) {
  std::unique_ptr<Rig> base = MakeRig(Options());
  if (base == nullptr) {
    return;
  }
  testing::Playing(base->harness);
  const RgbaImage before = base->Render();
  RigOptions options;
  const design_tokens::Rgb orange{.red = 1.0, .green = 0.5, .blue = 0.0};
  design_tokens::Color colour;
  colour.components = orange;
  colour.srgb = orange;
  options.ui.overrides.alumbra_token_player_sseekbar_splayed = colour;
  std::unique_ptr<Rig> changed = MakeRig(options);
  ASSERT_NE(changed, nullptr);
  testing::Playing(changed->harness);
  const RgbaImage after = changed->Render();
  changed->ui->Frame(changed->harness.Machine().NowMicros());
  const ControlLayout* seek =
      changed->ui->GetLayout().Find(umbra::ui::ControlId::kSeekBar);
  ASSERT_NE(seek, nullptr);
  const int x = static_cast<int>(seek->track.x + (seek->track.width / 10));
  const int y = static_cast<int>(seek->track.Centre().y);
  EXPECT_NE(Pixel(before, x, y), Pixel(after, x, y));
  const std::vector<std::uint8_t> pixel = Pixel(after, x, y);
  EXPECT_NEAR(pixel.at(0), Channel(1.0), 2);
  EXPECT_NEAR(pixel.at(1), Channel(0.5), 2);
  EXPECT_NEAR(pixel.at(2), Channel(0.0), 2);
}

TEST_F(DesktopUiGoldenTest, ReducedMotionKeepsTheSpinnerStill) {
  RigOptions options;
  options.ui.context.motion = design_tokens::Motion::kReduced;
  std::unique_ptr<Rig> rig = MakeRig(options);
  if (rig == nullptr) {
    return;
  }
  testing::Buffering(rig->harness);
  const RgbaImage first = rig->Render();
  rig->harness.AdvanceMillis(137);
  const RgbaImage second = rig->Render();
  EXPECT_EQ(first.pixels, second.pixels);
}

TEST_F(DesktopUiGoldenTest, FullMotionAnimatesTheSpinner) {
  std::unique_ptr<Rig> rig = MakeRig(Options());
  if (rig == nullptr) {
    return;
  }
  testing::Buffering(rig->harness);
  const RgbaImage first = rig->Render();
  rig->harness.AdvanceMillis(137);
  const RgbaImage second = rig->Render();
  EXPECT_NE(first.pixels, second.pixels);
}

TEST_F(DesktopUiGoldenTest, HiDpiScalesTheLayoutNotTheLogicalSize) {
  RigOptions options;
  options.density = 2.0F;
  std::unique_ptr<Rig> sharp = MakeRig(options);
  std::unique_ptr<Rig> plain = MakeRig(Options());
  if (sharp == nullptr || plain == nullptr) {
    return;
  }
  testing::Paused(sharp->harness);
  testing::Paused(plain->harness);
  const RgbaImage big = sharp->Render();
  const RgbaImage small = plain->Render();
  EXPECT_EQ(big.size.width, 2 * small.size.width);
  EXPECT_EQ(big.size.height, 2 * small.size.height);
  const ControlLayout* a =
      sharp->ui->GetLayout().Find(umbra::ui::ControlId::kPlayPause);
  const ControlLayout* b =
      plain->ui->GetLayout().Find(umbra::ui::ControlId::kPlayPause);
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(a->hit, b->hit);
  const int corner_x = static_cast<int>(a->hit.x) + 4;
  const int corner_y = static_cast<int>(a->hit.Centre().y);
  const std::vector<std::uint8_t> small_pixel =
      Pixel(small, corner_x, corner_y);
  const std::vector<std::uint8_t> big_pixel =
      Pixel(big, 2 * corner_x, 2 * corner_y);
  for (std::size_t channel = 0; channel < 3; ++channel) {
    EXPECT_NEAR(small_pixel.at(channel), big_pixel.at(channel), 3);
  }
}

TEST_F(DesktopUiGoldenTest, ReleasesEveryGpuResource) {
  std::unique_ptr<Rig> rig = MakeRig(Options());
  if (rig == nullptr) {
    return;
  }
  testing::Failed(rig->harness);
  static_cast<void>(rig->Render());
  const std::int64_t with_ui = rig->presenter->Context()->counters.Live();
  rig->ui.reset();
  const std::int64_t without_ui = rig->presenter->Context()->counters.Live();
  EXPECT_LT(without_ui, with_ui);
  for (int round = 0; round < 3; ++round) {
    absl::StatusOr<std::unique_ptr<DesktopUi>> ui = DesktopUi::Create(
        *rig->presenter, rig->harness.Machine(), DesktopUiOptions{});
    ASSERT_TRUE(ui.ok());
    rig->ui = *std::move(ui);
    static_cast<void>(rig->Render());
    EXPECT_EQ(rig->presenter->Context()->counters.Live(), with_ui);
    rig->ui.reset();
    EXPECT_EQ(rig->presenter->Context()->counters.Live(), without_ui);
  }
}

SDL_Event MouseButton(SDL_EventType type, float x, float y) {
  SDL_MouseButtonEvent button{};
  button.type = type;
  button.button = SDL_BUTTON_LEFT;
  button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
  button.clicks = 1;
  button.x = x;
  button.y = y;
  SDL_Event event{};
  std::memcpy(&event, &button, sizeof(button));
  return event;
}

SDL_Event MouseMotion(float x, float y) {
  SDL_MouseMotionEvent motion{};
  motion.type = SDL_EVENT_MOUSE_MOTION;
  motion.x = x;
  motion.y = y;
  SDL_Event event{};
  std::memcpy(&event, &motion, sizeof(motion));
  return event;
}

SDL_Event Key(SDL_Keycode key, SDL_Keymod modifiers = SDL_KMOD_NONE) {
  SDL_KeyboardEvent press{};
  press.type = SDL_EVENT_KEY_DOWN;
  press.key = key;
  press.mod = modifiers;
  press.down = true;
  SDL_Event event{};
  std::memcpy(&event, &press, sizeof(press));
  return event;
}

template <typename T>
std::vector<T> Collect(const std::vector<umbra::ui::SequencedEffect>& effects) {
  std::vector<T> found;
  for (const umbra::ui::SequencedEffect& sequenced : effects) {
    if (const auto* effect = std::get_if<T>(&sequenced.effect)) {
      found.push_back(*effect);
    }
  }
  return found;
}

class DesktopUiInputTest : public UiGpuTest {
 protected:
  void SetUpRig() {
    rig_ = MakeRig(Options());
    if (rig_ == nullptr) {
      return;
    }
    testing::Playing(rig_->harness);
    static_cast<void>(rig_->Render());
    rig_->effects.clear();
  }

  void Send(SDL_Event event) {
    SDL_PushEvent(&event);
    SDL_Event polled{};
    while (SDL_PollEvent(&polled)) {
      rig_->ui->HandleEvent(polled);
    }
  }

  std::unique_ptr<Rig> rig_;
};

TEST_F(DesktopUiInputTest, MouseClickOnPlayPauseSendsPause) {
  SetUpRig();
  if (rig_ == nullptr) {
    return;
  }
  const ControlLayout* control =
      rig_->ui->GetLayout().Find(umbra::ui::ControlId::kPlayPause);
  ASSERT_NE(control, nullptr);
  const Point centre = control->hit.Centre();
  Send(MouseMotion(centre.x, centre.y));
  Send(MouseButton(SDL_EVENT_MOUSE_BUTTON_DOWN, centre.x, centre.y));
  Send(MouseButton(SDL_EVENT_MOUSE_BUTTON_UP, centre.x, centre.y));
  EXPECT_EQ(Collect<umbra::ui::PlayerPause>(rig_->effects).size(), 1U);
}

TEST_F(DesktopUiInputTest, SpaceTogglesPlayback) {
  SetUpRig();
  if (rig_ == nullptr) {
    return;
  }
  Send(Key(SDLK_SPACE));
  EXPECT_EQ(Collect<umbra::ui::PlayerPause>(rig_->effects).size(), 1U);
  Send(Key(SDLK_K));
  EXPECT_EQ(Collect<umbra::ui::PlayerPlay>(rig_->effects).size(), 1U);
}

TEST_F(DesktopUiInputTest, DraggingTheSeekBarScrubsAndSeeksOnRelease) {
  SetUpRig();
  if (rig_ == nullptr) {
    return;
  }
  const ControlLayout* seek =
      rig_->ui->GetLayout().Find(umbra::ui::ControlId::kSeekBar);
  ASSERT_NE(seek, nullptr);
  const float y = seek->track.Centre().y;
  const float start = seek->track.x + (seek->track.width / 2);
  const float end = seek->track.x + (seek->track.width * 3 / 4);
  Send(MouseButton(SDL_EVENT_MOUSE_BUTTON_DOWN, start, y));
  Send(MouseMotion(end, y));
  Send(MouseButton(SDL_EVENT_MOUSE_BUTTON_UP, end, y));
  const std::vector<umbra::ui::PlayerSeek> seeks =
      Collect<umbra::ui::PlayerSeek>(rig_->effects);
  ASSERT_FALSE(seeks.empty());
  EXPECT_TRUE(seeks.back().precise);
  EXPECT_NEAR(static_cast<double>(seeks.back().position_us), 450'000'000.0,
              2'000'000.0);
}

TEST_F(DesktopUiInputTest, KeyboardAndPointerAdjustVolume) {
  SetUpRig();
  if (rig_ == nullptr) {
    return;
  }
  Send(Key(SDLK_UP));
  const std::vector<umbra::ui::PlayerSetVolume> keyed =
      Collect<umbra::ui::PlayerSetVolume>(rig_->effects);
  ASSERT_EQ(keyed.size(), 1U);
  EXPECT_NEAR(keyed.front().volume, 0.75, 1e-6);
  rig_->effects.clear();
  const ControlLayout* volume =
      rig_->ui->GetLayout().Find(umbra::ui::ControlId::kVolume);
  ASSERT_NE(volume, nullptr);
  const float y = volume->track.Centre().y;
  Send(MouseButton(SDL_EVENT_MOUSE_BUTTON_DOWN,
                   volume->track.x + (volume->track.width / 4), y));
  Send(MouseButton(SDL_EVENT_MOUSE_BUTTON_UP,
                   volume->track.x + (volume->track.width / 4), y));
  const std::vector<umbra::ui::PlayerSetVolume> dragged =
      Collect<umbra::ui::PlayerSetVolume>(rig_->effects);
  ASSERT_FALSE(dragged.empty());
  EXPECT_NEAR(dragged.back().volume, 0.25, 0.01);
}

TEST_F(DesktopUiInputTest, ArrowKeysSeekOneStep) {
  SetUpRig();
  if (rig_ == nullptr) {
    return;
  }
  Send(Key(SDLK_RIGHT));
  const std::vector<umbra::ui::PlayerSeek> seeks =
      Collect<umbra::ui::PlayerSeek>(rig_->effects);
  ASSERT_EQ(seeks.size(), 1U);
  EXPECT_NEAR(static_cast<double>(seeks.front().position_us), 233'000'000.0,
              1'500'000.0);
}

}
}
