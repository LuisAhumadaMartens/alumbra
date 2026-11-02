#include "alumbra/design_tokens/design_tokens.h"

#include <cstddef>
#include <set>
#include <string>
#include <string_view>

#include "gtest/gtest.h"

namespace alumbra::design_tokens {
namespace {

constexpr Tokens kDefaultTokens = Resolve();

static_assert(kDefaultTokens.alumbra_token_target_sminimum == 44.0);
static_assert(
    kDefaultTokens.alumbra_token_color_ssurface_sbase.components.blue == 0.094);
static_assert(kDefaultTokens.alumbra_token_motion_sduration_sshort ==
              Milliseconds(120.0));
static_assert(kDefaultTokens.alumbra_token_motion_seasing_sstandard.x1 == 0.2);
static_assert(
    Resolve({.density = Density::kTv}).alumbra_token_target_sminimum == 72.0);
static_assert(Resolve({.motion = Motion::kReduced})
                  .alumbra_token_motion_sduration_sshort == Milliseconds(0.0));

constexpr Color Opaque(double red, double green, double blue) {
  const Rgb components{.red = red, .green = green, .blue = blue};
  return {.color_space = ColorSpace::kSrgb,
          .components = components,
          .alpha = 1.0,
          .srgb = components};
}

bool Same(const Color& first, const Color& second) {
  return first.color_space == second.color_space &&
         first.components.red == second.components.red &&
         first.components.green == second.components.green &&
         first.components.blue == second.components.blue &&
         first.alpha == second.alpha && first.srgb.red == second.srgb.red &&
         first.srgb.green == second.srgb.green &&
         first.srgb.blue == second.srgb.blue;
}

TEST(DesignTokensTest, DefaultContextIsDarkNormalDesktopFullMotionSdr) {
  const Context context;
  EXPECT_EQ(Name(context.theme), "dark");
  EXPECT_EQ(Name(context.contrast), "normal");
  EXPECT_EQ(Name(context.density), "desktop");
  EXPECT_EQ(Name(context.motion), "full");
  EXPECT_EQ(Name(context.dynamic_range), "sdr");
  EXPECT_EQ(kDefaultTokens.alumbra_token_luminance_sreference_uwhite, 203.0);
  EXPECT_EQ(kDefaultTokens.alumbra_token_luminance_shdr_uhighlight_uceiling,
            203.0);
}

TEST(DesignTokensTest, ThemeAndContrastCombineAsTheResolverOrders) {
  const Color white = Opaque(1.0, 1.0, 1.0);
  const Color black = Opaque(0.0, 0.0, 0.0);
  EXPECT_TRUE(
      Same(Resolve({.theme = Theme::kLight}).alumbra_token_color_ssurface_sbase,
           white));
  EXPECT_TRUE(Same(
      Resolve({.contrast = Contrast::kHigh}).alumbra_token_color_ssurface_sbase,
      black));
  EXPECT_TRUE(
      Same(Resolve({.theme = Theme::kLight, .contrast = Contrast::kHigh})
               .alumbra_token_color_stext_sprimary,
           black));
  EXPECT_EQ(Resolve({.dynamic_range = DynamicRange::kHdr})
                .alumbra_token_luminance_shdr_uhighlight_uceiling,
            600.0);
}

TEST(DesignTokensTest, SemanticOverrideReachesEveryAliasingComponent) {
  const Color accent = Opaque(0.25, 0.5, 0.75);
  Overrides overrides;
  overrides.alumbra_token_color_scontrol_sbackground = accent;
  for (const Theme theme : kThemeValues) {
    const Tokens tokens = Resolve({.theme = theme}, overrides);
    EXPECT_TRUE(Same(tokens.alumbra_token_player_sbutton_sbackground, accent));
    EXPECT_TRUE(
        Same(tokens.alumbra_token_player_sbutton_sbackground_sfocus, accent));
    EXPECT_TRUE(Same(tokens.alumbra_token_player_sad_uchrome_sbadge_sbackground,
                     accent));
    EXPECT_FALSE(
        Same(tokens.alumbra_token_player_sbutton_sbackground_shover, accent));
  }
}

TEST(DesignTokensTest, ExplicitComponentOverrideWinsOverItsAlias) {
  const Color accent = Opaque(0.25, 0.5, 0.75);
  const Color focus = Opaque(0.125, 0.25, 0.375);
  Overrides overrides;
  overrides.alumbra_token_color_scontrol_sbackground = accent;
  overrides.alumbra_token_player_sbutton_sbackground_sfocus = focus;
  const Tokens tokens = Resolve({}, overrides);
  EXPECT_TRUE(Same(tokens.alumbra_token_player_sbutton_sbackground, accent));
  EXPECT_TRUE(
      Same(tokens.alumbra_token_player_sbutton_sbackground_sfocus, focus));
}

TEST(DesignTokensTest, UnderscoreAndDotPathsStayDistinct) {
  std::set<std::string> names;
  int visited = 0;
  auto visitor = [&names, &visited](std::string_view name, const auto&) {
    names.emplace(name);
    ++visited;
  };
  VisitTokens(kDefaultTokens, visitor);
  EXPECT_EQ(names.size(), static_cast<std::size_t>(visited));
  EXPECT_TRUE(names.contains("player.ad_band.background"));
  EXPECT_TRUE(names.contains("player.ad.band.background"));
  EXPECT_FALSE(Same(kDefaultTokens.alumbra_token_player_sad_uband_sbackground,
                    kDefaultTokens.alumbra_token_player_sad_sband_sbackground));
}

}
}
