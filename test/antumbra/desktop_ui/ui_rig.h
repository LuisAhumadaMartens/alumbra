#ifndef TEST_ANTUMBRA_DESKTOP_UI_UI_RIG_H_
#define TEST_ANTUMBRA_DESKTOP_UI_UI_RIG_H_

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "alumbra/desktop_ui/desktop_ui.h"
#include "gpu_test_environment.h"
#include "harness.h"
#include "penumbra/desktop/video/color_conversion.h"
#include "penumbra/desktop/video/gpu_presenter.h"
#include "penumbra/desktop/video/presenter.h"
#include "penumbra/desktop/video/video_geometry.h"
#include "penumbra/interface/stream_info.h"
#include "test_frames.h"
#include "umbra/ui/effect.h"

namespace alumbra::desktop_ui::testing {

inline constexpr std::int32_t kLogicalWidth = 480;
inline constexpr std::int32_t kLogicalHeight = 300;

struct RigOptions {
  float density = 1.0F;
  DesktopUiOptions ui;
  umbra::ui::UiConfig config;
};

inline penumbra::desktop::testing::TestFrame MakeBackdrop() {
  using penumbra::PixelFormat;
  penumbra::ColorSpace color;
  color.matrix = 1;
  penumbra::desktop::testing::TestFrame frame(PixelFormat::kI420, kLogicalWidth,
                                              kLogicalHeight, color);
  for (std::int32_t y = 0; y < kLogicalHeight; ++y) {
    for (std::int32_t x = 0; x < kLogicalWidth; ++x) {
      frame.SetLuma(x, y,
                    static_cast<std::uint16_t>(40 + (x * 60 / kLogicalWidth) +
                                               (y * 40 / kLogicalHeight)));
    }
  }
  for (std::int32_t y = 0; y < frame.Layout().chroma_height; ++y) {
    for (std::int32_t x = 0; x < frame.Layout().chroma_width; ++x) {
      frame.SetChroma(x, y, 150, 110);
    }
  }
  return frame;
}

struct Rig {
  explicit Rig(penumbra::desktop::testing::TestFrame backdrop_frame,
               const umbra::ui::UiConfig& config)
      : backdrop(std::move(backdrop_frame)), harness(config) {}

  std::unique_ptr<penumbra::desktop::GpuPresenter> presenter;
  penumbra::desktop::testing::TestFrame backdrop;
  umbra::ui::Harness harness;
  std::unique_ptr<DesktopUi> ui;
  std::vector<umbra::ui::SequencedEffect> effects;
  std::int64_t baseline_live = 0;

  penumbra::desktop::RgbaImage Render() {
    ui->Frame(harness.Machine().NowMicros());
    penumbra::desktop::PresentRequest request;
    request.frame = &backdrop.Frame();
    EXPECT_TRUE(presenter->Present(request).ok());
    absl::StatusOr<penumbra::desktop::RgbaImage> image =
        presenter->ReadbackTarget();
    EXPECT_TRUE(image.ok());
    return image.ok() ? *std::move(image) : penumbra::desktop::RgbaImage{};
  }
};

class UiGpuTest : public penumbra::desktop::testing::GpuTest {
 protected:
  static std::unique_ptr<Rig> MakeRig(const RigOptions& options) {
    penumbra::desktop::GpuPresenterOptions presenter_options;
    presenter_options.offscreen_logical_size = penumbra::desktop::PixelSize{
        .width = kLogicalWidth, .height = kLogicalHeight};
    presenter_options.offscreen_pixel_density = options.density;
    auto rig = std::make_unique<Rig>(MakeBackdrop(), options.config);
    rig->presenter = CreatePresenter(presenter_options);
    if (rig->presenter == nullptr) {
      return nullptr;
    }
    rig->baseline_live = rig->presenter->Context()->counters.Live();
    absl::StatusOr<std::unique_ptr<DesktopUi>> ui =
        DesktopUi::Create(*rig->presenter, rig->harness.Machine(), options.ui);
    EXPECT_TRUE(ui.ok()) << ui.status();
    if (!ui.ok()) {
      return nullptr;
    }
    rig->ui = *std::move(ui);
    Rig* raw = rig.get();
    raw->ui->SetEffectSink(
        [raw](const std::vector<umbra::ui::SequencedEffect>& effects) {
          raw->effects.insert(raw->effects.end(), effects.begin(),
                              effects.end());
        });
    return rig;
  }
};

}

#endif
