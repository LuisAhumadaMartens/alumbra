#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "alumbra/design_tokens/design_tokens.h"
#include "alumbra/desktop_ui/animation.h"
#include "alumbra/desktop_ui/compose.h"
#include "alumbra/desktop_ui/draw_list.h"
#include "alumbra/desktop_ui/embedded_font.h"
#include "alumbra/desktop_ui/font_face.h"
#include "alumbra/desktop_ui/geometry.h"
#include "alumbra/desktop_ui/glyph_atlas.h"
#include "alumbra/desktop_ui/icons.h"
#include "alumbra/desktop_ui/layout.h"
#include "alumbra/desktop_ui/strings.h"
#include "alumbra/desktop_ui/style.h"
#include "alumbra/desktop_ui/text_shaper.h"
#include "harness.h"
#include "scenarios.h"
#include "umbra/ui/types.h"
#include "umbra/ui/view_model.h"

namespace alumbra::desktop_ui {
namespace {

const TextShaper& Shaper() {
  static const std::unique_ptr<TextShaper> shaper =
      *TextShaper::Create(EmbeddedFontData());
  return *shaper;
}

float Advance(const std::string& text) {
  return Shaper().Shape(text, TextDirection::kLeftToRight).advance;
}

TEST(TextShapingTest, ShapesLatinTextIntoOneGlyphPerLetter) {
  const ShapedText shaped = Shaper().Shape("Play", TextDirection::kLeftToRight);
  EXPECT_EQ(shaped.glyphs.size(), 4U);
  EXPECT_GT(shaped.advance, 0.0F);
  for (std::size_t index = 0; index < shaped.glyphs.size(); ++index) {
    EXPECT_EQ(shaped.glyphs.at(index).cluster, index);
    EXPECT_NE(shaped.glyphs.at(index).glyph, 0U);
  }
}

TEST(TextShapingTest, AppliesKerningFromTheFont) {
  const float pair = Advance("AV");
  const float separate = Advance("A") + Advance("V");
  EXPECT_LT(pair, separate);
}

TEST(TextShapingTest, TabularNumbersKeepDigitsTheSameWidth) {
  EXPECT_FLOAT_EQ(Advance("1"), Advance("0"));
  EXPECT_FLOAT_EQ(Advance("1:11"), Advance("0:00"));
}

TEST(TextShapingTest, RightToLeftDirectionReversesTheVisualOrder) {
  const ShapedText ltr = Shaper().Shape("abc", TextDirection::kLeftToRight);
  const ShapedText rtl = Shaper().Shape("abc", TextDirection::kRightToLeft);
  ASSERT_EQ(rtl.glyphs.size(), 3U);
  EXPECT_EQ(ltr.glyphs.front().cluster, 0U);
  EXPECT_EQ(rtl.glyphs.front().cluster, 2U);
  EXPECT_EQ(rtl.glyphs.front().glyph, ltr.glyphs.back().glyph);
  EXPECT_NEAR(rtl.advance, ltr.advance, 0.05F);
}

TEST(TextShapingTest, EmptyTextShapesToNothing) {
  const ShapedText shaped = Shaper().Shape("", TextDirection::kLeftToRight);
  EXPECT_TRUE(shaped.glyphs.empty());
  EXPECT_FLOAT_EQ(shaped.advance, 0.0F);
}

TEST(TextShapingTest, MetricsDescribeAnEmBox) {
  const FontMetrics metrics = Shaper().Metrics();
  EXPECT_GT(metrics.ascent, 0.5F);
  EXPECT_GT(metrics.descent, 0.0F);
  EXPECT_LT(metrics.ascent + metrics.descent, 1.6F);
}

TEST(GlyphAtlasTest, RasterisesOnceAndReusesTheEntry) {
  const std::unique_ptr<FontFace> face = *FontFace::Create(EmbeddedFontData());
  GlyphAtlas atlas(256);
  const ShapedText shaped = Shaper().Shape("H", TextDirection::kLeftToRight);
  const auto first = atlas.Lookup(*face, shaped.glyphs.front().glyph, 16.0F);
  ASSERT_TRUE(first.has_value());
  EXPECT_GT(first->width, 0);
  EXPECT_TRUE(atlas.Dirty());
  atlas.ClearDirty();
  const auto second = atlas.Lookup(*face, shaped.glyphs.front().glyph, 16.0F);
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(first->u0, second->u0);
  EXPECT_FALSE(atlas.Dirty());
  EXPECT_EQ(atlas.GlyphCount(), 1U);
  const auto larger = atlas.Lookup(*face, shaped.glyphs.front().glyph, 32.0F);
  ASSERT_TRUE(larger.has_value());
  EXPECT_GT(larger->width, first->width);
}

TEST(GlyphAtlasTest, ReportsFullnessAndRecoversAfterReset) {
  const std::unique_ptr<FontFace> face = *FontFace::Create(EmbeddedFontData());
  GlyphAtlas atlas(32);
  const ShapedText shaped = Shaper().Shape("HW", TextDirection::kLeftToRight);
  EXPECT_TRUE(
      atlas.Lookup(*face, shaped.glyphs.at(0).glyph, 20.0F).has_value());
  EXPECT_FALSE(
      atlas.Lookup(*face, shaped.glyphs.at(1).glyph, 28.0F).has_value());
  atlas.Reset();
  EXPECT_EQ(atlas.GlyphCount(), 0U);
  EXPECT_TRUE(
      atlas.Lookup(*face, shaped.glyphs.at(1).glyph, 20.0F).has_value());
}

TEST(IconTest, MapsTokensAndFallsBackToADot) {
  EXPECT_EQ(IconFromToken("icon.play"), IconId::kPlay);
  EXPECT_EQ(IconFromToken("icon.volume_off"), IconId::kVolumeOff);
  EXPECT_EQ(IconFromToken("icon.something_custom"), IconId::kDot);
}

TEST(IconTest, SignedDistanceFieldsAreInsideAtTheirCentres) {
  const IconSdfAtlas atlas;
  for (std::size_t index = 0; index < kIconCount; ++index) {
    const IconAtlasCell cell = IconSdfAtlas::Cell(static_cast<IconId>(index));
    const int size = IconSdfAtlas::Size();
    const int x =
        static_cast<int>((cell.u0 + cell.u1) / 2 * static_cast<float>(size));
    const int y =
        static_cast<int>((cell.v0 + cell.v1) / 2 * static_cast<float>(size));
    std::uint8_t maximum = 0;
    std::uint8_t minimum = 255;
    for (int row = -20; row <= 20; ++row) {
      for (int column = -20; column <= 20; ++column) {
        const std::uint8_t value =
            atlas.Pixels().at((static_cast<std::size_t>(y + row) *
                               static_cast<std::size_t>(size)) +
                              static_cast<std::size_t>(x + column));
        maximum = std::max(maximum, value);
        minimum = std::min(minimum, value);
      }
    }
    EXPECT_GT(maximum, 128) << index;
    EXPECT_LT(minimum, 128) << index;
  }
}

TEST(StringsTest, FormatsClocksAndPlaceholders) {
  EXPECT_EQ(FormatClock(0, false), "0:00");
  EXPECT_EQ(FormatClock(75'000'000, false), "1:15");
  EXPECT_EQ(FormatClock(3'725'000'000, false), "1:02:05");
  const StringCatalogue english;
  EXPECT_EQ(english.Resolve("error.message",
                            {umbra::ui::TextArg{.name = "code",
                                                .value = std::int64_t{1001}}}),
            "Playback stopped. Error 1001.");
}

TEST(StringsTest, PseudoLocalesAccentAndExpandButKeepPlaceholders) {
  const std::string accented = PseudoAccent("Error {code}");
  EXPECT_EQ(accented.front(), '[');
  EXPECT_NE(accented.find("{code}"), std::string::npos);
  EXPECT_NE(accented.find("\xC3\x89"), std::string::npos);
  EXPECT_GT(accented.size(), std::string("Error {code}").size());
  const StringCatalogue mirrored(Locale::kPseudoMirrored);
  EXPECT_EQ(mirrored.Direction(), TextDirection::kRightToLeft);
  EXPECT_EQ(StringCatalogue(Locale::kPseudoAccented).Direction(),
            TextDirection::kLeftToRight);
}

TEST(AnimationTest, EasingStartsAtZeroEndsAtOneAndIsMonotonic) {
  const design_tokens::CubicBezier curve{
      .x1 = 0.2, .y1 = 0.0, .x2 = 0.0, .y2 = 1.0};
  EXPECT_NEAR(EvaluateEasing(curve, 0.0F), 0.0F, 1e-3);
  EXPECT_NEAR(EvaluateEasing(curve, 1.0F), 1.0F, 1e-3);
  float previous = 0;
  for (int step = 1; step <= 10; ++step) {
    const float value = EvaluateEasing(curve, static_cast<float>(step) / 10);
    EXPECT_GE(value, previous);
    previous = value;
  }
}

TEST(AnimationTest, ChromeFadeRunsForTheMotionDuration) {
  const design_tokens::Tokens tokens = design_tokens::Resolve();
  const Style style = StyleOf(tokens);
  ChromeFade fade;
  EXPECT_FLOAT_EQ(fade.Advance(true, 0, style), 1.0F);
  EXPECT_FLOAT_EQ(fade.Advance(false, 1000, style), 1.0F);
  const float middle = fade.Advance(false, 1000 + (style.fade_us / 2), style);
  EXPECT_GT(middle, 0.0F);
  EXPECT_LT(middle, 1.0F);
  EXPECT_FALSE(fade.Settled());
  EXPECT_FLOAT_EQ(fade.Advance(false, 1000 + style.fade_us, style), 0.0F);
  EXPECT_TRUE(fade.Settled());
}

TEST(AnimationTest, ReducedMotionTurnsFadesAndSpinnersOff) {
  design_tokens::Context context;
  context.motion = design_tokens::Motion::kReduced;
  const Style style = StyleOf(design_tokens::Resolve(context));
  EXPECT_EQ(style.fade_us, 0);
  EXPECT_EQ(style.spinner_period_us, 0);
  ChromeFade fade;
  EXPECT_FLOAT_EQ(fade.Advance(true, 0, style), 1.0F);
  EXPECT_FLOAT_EQ(fade.Advance(false, 1, style), 0.0F);
  EXPECT_FLOAT_EQ(SpinnerPhase(123456, style.spinner_period_us), 0.0F);
}

class LayoutTest : public ::testing::Test {
 protected:
  Layout Compute(void (*scenario)(umbra::ui::Harness&), Size viewport,
                 const design_tokens::Context& context = {},
                 Locale locale = Locale::kEnglish) {
    harness_ = std::make_unique<umbra::ui::Harness>();
    scenario(*harness_);
    snapshot_ = harness_->Snapshot();
    tokens_ = design_tokens::Resolve(context);
    catalogue_ = StringCatalogue(locale);
    LayoutInput input;
    input.viewport = viewport;
    input.view_model = snapshot_.get();
    input.tokens = &tokens_;
    input.shaper = &Shaper();
    input.catalogue = &catalogue_;
    return ComputeLayout(input);
  }

  std::unique_ptr<umbra::ui::Harness> harness_;
  std::shared_ptr<const umbra::ui::ViewModel> snapshot_;
  design_tokens::Tokens tokens_;
  StringCatalogue catalogue_;
};

TEST_F(LayoutTest, EveryControlMeetsTheMinimumTargetAndStaysInside) {
  const Layout layout = Compute(testing::Playing, Size{480, 270});
  const auto target = static_cast<float>(tokens_.alumbra_token_target_sminimum);
  ASSERT_GE(layout.controls.size(), 5U);
  for (const ControlLayout& control : layout.controls) {
    EXPECT_GE(control.hit.height, target);
    EXPECT_GE(control.hit.x, 0.0F);
    EXPECT_LE(control.hit.Right(), 480.0F);
    EXPECT_LE(control.hit.Bottom(), 270.0F);
    EXPECT_TRUE(layout.band.Intersected(control.hit).width > 0);
  }
}

TEST_F(LayoutTest, ControlsDoNotOverlap) {
  const Layout layout = Compute(testing::Playing, Size{480, 270});
  for (std::size_t first = 0; first < layout.controls.size(); ++first) {
    for (std::size_t second = first + 1; second < layout.controls.size();
         ++second) {
      const Rect overlap = layout.controls.at(first).hit.Intersected(
          layout.controls.at(second).hit);
      EXPECT_TRUE(overlap.Empty()) << first << " and " << second;
    }
  }
  ASSERT_TRUE(layout.time_box.has_value());
  for (const ControlLayout& control : layout.controls) {
    EXPECT_TRUE(control.hit.Intersected(*layout.time_box).Empty());
  }
}

TEST_F(LayoutTest, LeadingControlsRunLeftToRightAndTrailingSitAtTheRight) {
  const Layout layout = Compute(testing::Playing, Size{480, 270});
  const ControlLayout* play = layout.Find(umbra::ui::ControlId::kPlayPause);
  const ControlLayout* mute = layout.Find(umbra::ui::ControlId::kMute);
  const ControlLayout* fullscreen =
      layout.Find(umbra::ui::ControlId::kFullscreen);
  ASSERT_NE(play, nullptr);
  ASSERT_NE(mute, nullptr);
  ASSERT_NE(fullscreen, nullptr);
  EXPECT_LT(play->hit.x, mute->hit.x);
  EXPECT_GT(fullscreen->hit.x, mute->hit.x);
  EXPECT_LE(fullscreen->hit.Right(), 480.0F);
  const ControlLayout* seek = layout.Find(umbra::ui::ControlId::kSeekBar);
  ASSERT_NE(seek, nullptr);
  EXPECT_LT(seek->hit.Bottom(), play->hit.y + 1.0F);
}

TEST_F(LayoutTest, TheControlBarDoesNotMirrorInRightToLeft) {
  const Layout plain = Compute(testing::Playing, Size{480, 270});
  const Layout mirrored =
      Compute(testing::Playing, Size{480, 270}, {}, Locale::kPseudoMirrored);
  EXPECT_EQ(plain.Find(umbra::ui::ControlId::kPlayPause)->hit,
            mirrored.Find(umbra::ui::ControlId::kPlayPause)->hit);
}

TEST_F(LayoutTest, NarrowWindowsDropTheVolumeSliderThenTheClock) {
  const Layout wide = Compute(testing::Playing, Size{480, 270});
  EXPECT_NE(wide.Find(umbra::ui::ControlId::kVolume), nullptr);
  ASSERT_TRUE(wide.time_box.has_value());
  const Layout narrow = Compute(testing::Playing, Size{320, 200});
  EXPECT_EQ(narrow.Find(umbra::ui::ControlId::kVolume), nullptr);
  EXPECT_NE(narrow.Find(umbra::ui::ControlId::kPlayPause), nullptr);
  for (const ControlLayout& control : narrow.controls) {
    EXPECT_LE(control.hit.Right(), 320.0F);
  }
}

TEST_F(LayoutTest, TokenModesChangeTheMetricsWithoutRendererChanges) {
  design_tokens::Context tv;
  tv.density = design_tokens::Density::kTv;
  const Layout desktop = Compute(testing::Paused, Size{960, 540});
  const Layout big = Compute(testing::Paused, Size{960, 540}, tv);
  EXPECT_GT(big.controls.front().hit.height, 0.0F);
  EXPECT_GT(big.band.height, desktop.band.height);
}

TEST_F(LayoutTest, ErrorPanelIsCentredAndStartAligned) {
  const Layout layout = Compute(testing::Failed, Size{480, 270});
  ASSERT_TRUE(layout.error.has_value());
  const Rect panel = layout.error->panel;
  EXPECT_NEAR(panel.Centre().x, 240.0F, 0.5F);
  EXPECT_NEAR(panel.Centre().y, layout.band.y / 2, 0.5F);
  EXPECT_LT(layout.error->icon.x, panel.Centre().x);
  EXPECT_FALSE(layout.error->title.shaped.glyphs.empty());
  const Layout mirrored =
      Compute(testing::Failed, Size{480, 270}, {}, Locale::kPseudoMirrored);
  ASSERT_TRUE(mirrored.error.has_value());
  EXPECT_GT(mirrored.error->icon.x, mirrored.error->panel.Centre().x);
}

TEST_F(LayoutTest, SpinnerOnlyAppearsWhileBuffering) {
  EXPECT_FALSE(Compute(testing::Playing, Size{480, 270}).spinner.has_value());
  const Layout layout = Compute(testing::Buffering, Size{480, 270});
  ASSERT_TRUE(layout.spinner.has_value());
  EXPECT_NEAR(layout.spinner->centre.x, 240.0F, 0.01F);
}

TEST_F(LayoutTest, HitTestAndSliderFractions) {
  const Layout layout = Compute(testing::Playing, Size{480, 270});
  const ControlLayout* seek = layout.Find(umbra::ui::ControlId::kSeekBar);
  ASSERT_NE(seek, nullptr);
  EXPECT_EQ(layout.HitTest(seek->hit.Centre()), umbra::ui::ControlId::kSeekBar);
  EXPECT_FALSE(layout.HitTest(Point{1, 1}).has_value());
  EXPECT_DOUBLE_EQ(SliderFraction(*seek, seek->track.x - 50), 0.0);
  EXPECT_DOUBLE_EQ(SliderFraction(*seek, seek->track.Right() + 50), 1.0);
  EXPECT_NEAR(SliderFraction(*seek, seek->track.x + (seek->track.width / 2)),
              0.5, 1e-6);
}

class ComposeTest : public LayoutTest {
 protected:
  DrawList Run(void (*scenario)(umbra::ui::Harness&), float chrome_alpha,
               std::int64_t now_us = 0) {
    const Layout layout = Compute(scenario, Size{480, 270});
    layout_ = layout;
    FrameInput input;
    input.view_model = snapshot_.get();
    input.layout = &layout_;
    input.tokens = &tokens_;
    input.shaper = &Shaper();
    input.catalogue = &catalogue_;
    input.now_us = now_us;
    input.chrome_alpha = chrome_alpha;
    return Compose(input);
  }

  Layout layout_;
};

TEST_F(ComposeTest, IsADeterministicFunctionOfItsInputs) {
  const DrawList first = Run(testing::Playing, 1.0F, 1234);
  const DrawList second = Run(testing::Playing, 1.0F, 1234);
  EXPECT_EQ(first, second);
  EXPECT_EQ(first.Describe(), second.Describe());
  EXPECT_FALSE(first.Empty());
}

TEST_F(ComposeTest, HiddenChromeDrawsNothing) {
  const DrawList list = Run(testing::Playing, 0.0F);
  EXPECT_TRUE(list.Empty());
}

TEST_F(ComposeTest, ChromeAlphaScalesEveryChromeColour) {
  const DrawList list = Run(testing::Playing, 0.5F);
  for (const DrawCommand& command : list.Commands()) {
    if (const auto* box = std::get_if<RoundedRectCommand>(&command)) {
      EXPECT_LE(box->fill.alpha, 0.5F + 1e-6F);
    }
  }
}

TEST_F(ComposeTest, TheSeekThumbFollowsTheExtrapolatedPlayhead) {
  Compute(testing::Playing, Size{480, 270});
  const std::int64_t base = snapshot_->seek_bar.playhead.anchor_us;
  EXPECT_EQ(ExtrapolatedPosition(snapshot_->seek_bar, base), 228'000'000);
  EXPECT_EQ(ExtrapolatedPosition(snapshot_->seek_bar, base + 2'000'000),
            230'000'000);
  EXPECT_EQ(ExtrapolatedPosition(snapshot_->seek_bar, base + 900'000'000'000),
            600'000'000);
  EXPECT_NEAR(PlayheadFraction(snapshot_->seek_bar, 300'000'000), 0.5, 1e-9);
}

TEST_F(ComposeTest, PositionsAdvanceAtDisplayRatesWithoutNewSnapshots) {
  Compute(testing::Playing, Size{480, 270});
  const std::int64_t base = snapshot_->seek_bar.playhead.anchor_us;
  std::int64_t previous = ExtrapolatedPosition(snapshot_->seek_bar, base);
  const std::int64_t frame_us = 1'000'000 / 144;
  for (int frame = 1; frame <= 144; ++frame) {
    const std::int64_t position =
        ExtrapolatedPosition(snapshot_->seek_bar, base + (frame * frame_us));
    EXPECT_GT(position, previous);
    previous = position;
  }
}

TEST_F(ComposeTest, TheErrorPanelAndSpinnerComeFromTheModes) {
  const DrawList error = Run(testing::Failed, 1.0F);
  EXPECT_NE(error.Describe().find("icon"), std::string::npos);
  EXPECT_NE(error.Describe().find("clip"), std::string::npos);
  const DrawList spinner = Run(testing::Buffering, 1.0F, 100'000);
  std::size_t dots = 0;
  for (const DrawCommand& command : spinner.Commands()) {
    if (const auto* box = std::get_if<RoundedRectCommand>(&command)) {
      if (box->rect.width == box->rect.height && box->rect.width < 20) {
        ++dots;
      }
    }
  }
  EXPECT_GE(dots, 8U);
}

TEST_F(ComposeTest, TheFocusedControlGetsARingInTheFocusToken) {
  const DrawList list = Run(testing::Focused, 1.0F);
  const Rgba ring = FromToken(tokens_.alumbra_token_player_sfocus_sring);
  int rings = 0;
  for (const DrawCommand& command : list.Commands()) {
    if (const auto* box = std::get_if<RoundedRectCommand>(&command)) {
      if (box->border_width > 0 && box->fill.alpha == 0 &&
          box->border == ring) {
        ++rings;
      }
    }
  }
  EXPECT_EQ(rings, 1);
}

}
}
