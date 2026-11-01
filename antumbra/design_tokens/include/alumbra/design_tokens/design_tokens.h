#ifndef ALUMBRA_DESIGN_TOKENS_DESIGN_TOKENS_H_
#define ALUMBRA_DESIGN_TOKENS_DESIGN_TOKENS_H_

#include <array>
#include <chrono>
#include <cstddef>
#include <optional>
#include <ratio>
#include <span>
#include <string_view>

namespace alumbra::design_tokens {

enum class Theme { kDark, kLight };
enum class Contrast { kNormal, kHigh };
enum class Density { kPhone, kTablet, kDesktop, kTv };
enum class Motion { kFull, kReduced };
enum class DynamicRange { kSdr, kHdr };

struct Context {
  Theme theme = Theme::kDark;
  Contrast contrast = Contrast::kNormal;
  Density density = Density::kDesktop;
  Motion motion = Motion::kFull;
  DynamicRange dynamic_range = DynamicRange::kSdr;
};

inline constexpr std::array<Theme, 2> kThemeValues = {{
    Theme::kDark,
    Theme::kLight,
}};
inline constexpr std::array<Contrast, 2> kContrastValues = {{
    Contrast::kNormal,
    Contrast::kHigh,
}};
inline constexpr std::array<Density, 4> kDensityValues = {{
    Density::kPhone,
    Density::kTablet,
    Density::kDesktop,
    Density::kTv,
}};
inline constexpr std::array<Motion, 2> kMotionValues = {{
    Motion::kFull,
    Motion::kReduced,
}};
inline constexpr std::array<DynamicRange, 2> kDynamicRangeValues = {{
    DynamicRange::kSdr,
    DynamicRange::kHdr,
}};

[[nodiscard]] constexpr std::string_view Name(Theme value) {
  switch (value) {
    case Theme::kDark:
      return "dark";
    case Theme::kLight:
      return "light";
  }
  return {};
}

[[nodiscard]] constexpr std::string_view Name(Contrast value) {
  switch (value) {
    case Contrast::kNormal:
      return "normal";
    case Contrast::kHigh:
      return "high";
  }
  return {};
}

[[nodiscard]] constexpr std::string_view Name(Density value) {
  switch (value) {
    case Density::kPhone:
      return "phone";
    case Density::kTablet:
      return "tablet";
    case Density::kDesktop:
      return "desktop";
    case Density::kTv:
      return "tv";
  }
  return {};
}

[[nodiscard]] constexpr std::string_view Name(Motion value) {
  switch (value) {
    case Motion::kFull:
      return "full";
    case Motion::kReduced:
      return "reduced";
  }
  return {};
}

[[nodiscard]] constexpr std::string_view Name(DynamicRange value) {
  switch (value) {
    case DynamicRange::kSdr:
      return "sdr";
    case DynamicRange::kHdr:
      return "hdr";
  }
  return {};
}

enum class ColorSpace { kSrgb, kDisplayP3, kRec2020 };

struct Rgb {
  double red = 0.0;
  double green = 0.0;
  double blue = 0.0;
};

struct Color {
  ColorSpace color_space = ColorSpace::kSrgb;
  Rgb components;
  double alpha = 1.0;
  Rgb srgb;
};

struct CubicBezier {
  double x1 = 0.0;
  double y1 = 0.0;
  double x2 = 1.0;
  double y2 = 1.0;
};

struct ShadowLayer {
  Color color;
  double offset_x = 0.0;
  double offset_y = 0.0;
  double blur = 0.0;
  double spread = 0.0;
  bool inset = false;
};

using Pixels = double;
using Milliseconds = std::chrono::duration<double, std::milli>;
using FontFamily = std::span<const std::string_view>;
using Shadow = std::span<const ShadowLayer>;

struct Tokens {
  Color alumbra_token_color_saccent_splayed;
  Color alumbra_token_color_sborder_ssubtle;
  Color alumbra_token_color_scontrol_sbackground;
  Color alumbra_token_color_scontrol_sdisabled;
  Color alumbra_token_color_scontrol_sforeground;
  Color alumbra_token_color_scontrol_shover;
  Color alumbra_token_color_scontrol_spressed;
  Color alumbra_token_color_sfocus_sring;
  Color alumbra_token_color_ssurface_sbase;
  Color alumbra_token_color_ssurface_selevated;
  Color alumbra_token_color_ssurface_soverlay;
  Color alumbra_token_color_ssurface_sscrim;
  Color alumbra_token_color_stext_sdisabled;
  Color alumbra_token_color_stext_sprimary;
  Color alumbra_token_color_stext_ssecondary;
  Shadow alumbra_token_elevation_smodal;
  Shadow alumbra_token_elevation_spanel;
  Shadow alumbra_token_elevation_sraised;
  double alumbra_token_layer_sad_uchrome = 0.0;
  double alumbra_token_layer_sad_usdk_ucontainer = 0.0;
  double alumbra_token_layer_scaptions = 0.0;
  double alumbra_token_layer_scontrols = 0.0;
  double alumbra_token_layer_svideo_usurface = 0.0;
  double alumbra_token_luminance_shdr_uhighlight_uceiling = 0.0;
  double alumbra_token_luminance_sreference_uwhite = 0.0;
  Milliseconds alumbra_token_motion_sduration_sinstant{};
  Milliseconds alumbra_token_motion_sduration_slong{};
  Milliseconds alumbra_token_motion_sduration_smedium{};
  Milliseconds alumbra_token_motion_sduration_sshort{};
  CubicBezier alumbra_token_motion_seasing_semphasized;
  CubicBezier alumbra_token_motion_seasing_sstandard;
  double alumbra_token_opacity_sdisabled = 0.0;
  Pixels alumbra_token_radius_slarge = 0.0;
  Pixels alumbra_token_radius_smedium = 0.0;
  Pixels alumbra_token_radius_spill = 0.0;
  Pixels alumbra_token_radius_ssmall = 0.0;
  Pixels alumbra_token_spacing_sextra_ularge = 0.0;
  Pixels alumbra_token_spacing_slarge = 0.0;
  Pixels alumbra_token_spacing_smedium = 0.0;
  Pixels alumbra_token_spacing_snone = 0.0;
  Pixels alumbra_token_spacing_ssmall = 0.0;
  Pixels alumbra_token_spacing_stight = 0.0;
  Pixels alumbra_token_target_sminimum = 0.0;
  FontFamily alumbra_token_typography_sfont_ufamily_sui;
  Pixels alumbra_token_typography_sfont_usize_sbody = 0.0;
  Pixels alumbra_token_typography_sfont_usize_scaption = 0.0;
  Pixels alumbra_token_typography_sfont_usize_sdisplay = 0.0;
  Pixels alumbra_token_typography_sfont_usize_stitle = 0.0;
  double alumbra_token_typography_sfont_uweight_sbold = 0.0;
  double alumbra_token_typography_sfont_uweight_smedium = 0.0;
  double alumbra_token_typography_sfont_uweight_sregular = 0.0;
  double alumbra_token_typography_sline_uheight_sbody = 0.0;
  double alumbra_token_typography_sline_uheight_scompact = 0.0;
  Color alumbra_token_player_sad_sband_sbackground;
  Color alumbra_token_player_sad_uband_sbackground;
  Color alumbra_token_player_sad_uchrome_sbadge_sbackground;
  Color alumbra_token_player_sbutton_sbackground;
  Color alumbra_token_player_sbutton_sbackground_sdisabled;
  Color alumbra_token_player_sbutton_sbackground_sfocus;
  Color alumbra_token_player_sbutton_sbackground_shover;
  Color alumbra_token_player_sbutton_sbackground_spressed;
  Color alumbra_token_player_sbutton_sforeground;
  Color alumbra_token_player_sbutton_sforeground_sdisabled;
  Color alumbra_token_player_sbutton_sforeground_sfocus;
  Color alumbra_token_player_sbutton_sforeground_shover;
  Color alumbra_token_player_sbutton_sforeground_spressed;
  Pixels alumbra_token_player_sbutton_sradius = 0.0;
  Pixels alumbra_token_player_scaptions_sfont_usize = 0.0;
  Color alumbra_token_player_scaptions_stext;
  Color alumbra_token_player_scaptions_swindow;
  Color alumbra_token_player_sfocus_sring;
  Color alumbra_token_player_sseekbar_splayed;
  Color alumbra_token_player_sseekbar_strack;
};

struct Overrides {
  std::optional<Color> alumbra_token_color_saccent_splayed;
  std::optional<Color> alumbra_token_color_sborder_ssubtle;
  std::optional<Color> alumbra_token_color_scontrol_sbackground;
  std::optional<Color> alumbra_token_color_scontrol_sdisabled;
  std::optional<Color> alumbra_token_color_scontrol_sforeground;
  std::optional<Color> alumbra_token_color_scontrol_shover;
  std::optional<Color> alumbra_token_color_scontrol_spressed;
  std::optional<Color> alumbra_token_color_sfocus_sring;
  std::optional<Color> alumbra_token_color_ssurface_sbase;
  std::optional<Color> alumbra_token_color_ssurface_selevated;
  std::optional<Color> alumbra_token_color_ssurface_soverlay;
  std::optional<Color> alumbra_token_color_ssurface_sscrim;
  std::optional<Color> alumbra_token_color_stext_sdisabled;
  std::optional<Color> alumbra_token_color_stext_sprimary;
  std::optional<Color> alumbra_token_color_stext_ssecondary;
  std::optional<Shadow> alumbra_token_elevation_smodal;
  std::optional<Shadow> alumbra_token_elevation_spanel;
  std::optional<Shadow> alumbra_token_elevation_sraised;
  std::optional<double> alumbra_token_layer_sad_uchrome;
  std::optional<double> alumbra_token_layer_sad_usdk_ucontainer;
  std::optional<double> alumbra_token_layer_scaptions;
  std::optional<double> alumbra_token_layer_scontrols;
  std::optional<double> alumbra_token_layer_svideo_usurface;
  std::optional<double> alumbra_token_luminance_shdr_uhighlight_uceiling;
  std::optional<double> alumbra_token_luminance_sreference_uwhite;
  std::optional<Milliseconds> alumbra_token_motion_sduration_sinstant;
  std::optional<Milliseconds> alumbra_token_motion_sduration_slong;
  std::optional<Milliseconds> alumbra_token_motion_sduration_smedium;
  std::optional<Milliseconds> alumbra_token_motion_sduration_sshort;
  std::optional<CubicBezier> alumbra_token_motion_seasing_semphasized;
  std::optional<CubicBezier> alumbra_token_motion_seasing_sstandard;
  std::optional<double> alumbra_token_opacity_sdisabled;
  std::optional<Pixels> alumbra_token_radius_slarge;
  std::optional<Pixels> alumbra_token_radius_smedium;
  std::optional<Pixels> alumbra_token_radius_spill;
  std::optional<Pixels> alumbra_token_radius_ssmall;
  std::optional<Pixels> alumbra_token_spacing_sextra_ularge;
  std::optional<Pixels> alumbra_token_spacing_slarge;
  std::optional<Pixels> alumbra_token_spacing_smedium;
  std::optional<Pixels> alumbra_token_spacing_snone;
  std::optional<Pixels> alumbra_token_spacing_ssmall;
  std::optional<Pixels> alumbra_token_spacing_stight;
  std::optional<Pixels> alumbra_token_target_sminimum;
  std::optional<FontFamily> alumbra_token_typography_sfont_ufamily_sui;
  std::optional<Pixels> alumbra_token_typography_sfont_usize_sbody;
  std::optional<Pixels> alumbra_token_typography_sfont_usize_scaption;
  std::optional<Pixels> alumbra_token_typography_sfont_usize_sdisplay;
  std::optional<Pixels> alumbra_token_typography_sfont_usize_stitle;
  std::optional<double> alumbra_token_typography_sfont_uweight_sbold;
  std::optional<double> alumbra_token_typography_sfont_uweight_smedium;
  std::optional<double> alumbra_token_typography_sfont_uweight_sregular;
  std::optional<double> alumbra_token_typography_sline_uheight_sbody;
  std::optional<double> alumbra_token_typography_sline_uheight_scompact;
  std::optional<Color> alumbra_token_player_sad_sband_sbackground;
  std::optional<Color> alumbra_token_player_sad_uband_sbackground;
  std::optional<Color> alumbra_token_player_sad_uchrome_sbadge_sbackground;
  std::optional<Color> alumbra_token_player_sbutton_sbackground;
  std::optional<Color> alumbra_token_player_sbutton_sbackground_sdisabled;
  std::optional<Color> alumbra_token_player_sbutton_sbackground_sfocus;
  std::optional<Color> alumbra_token_player_sbutton_sbackground_shover;
  std::optional<Color> alumbra_token_player_sbutton_sbackground_spressed;
  std::optional<Color> alumbra_token_player_sbutton_sforeground;
  std::optional<Color> alumbra_token_player_sbutton_sforeground_sdisabled;
  std::optional<Color> alumbra_token_player_sbutton_sforeground_sfocus;
  std::optional<Color> alumbra_token_player_sbutton_sforeground_shover;
  std::optional<Color> alumbra_token_player_sbutton_sforeground_spressed;
  std::optional<Pixels> alumbra_token_player_sbutton_sradius;
  std::optional<Pixels> alumbra_token_player_scaptions_sfont_usize;
  std::optional<Color> alumbra_token_player_scaptions_stext;
  std::optional<Color> alumbra_token_player_scaptions_swindow;
  std::optional<Color> alumbra_token_player_sfocus_sring;
  std::optional<Color> alumbra_token_player_sseekbar_splayed;
  std::optional<Color> alumbra_token_player_sseekbar_strack;
};

namespace internal {

inline constexpr unsigned kTheme = 1U;
inline constexpr unsigned kContrast = 2U;
inline constexpr unsigned kDensity = 4U;
inline constexpr unsigned kMotion = 8U;
inline constexpr unsigned kDynamicRange = 16U;

[[nodiscard]] constexpr Rgb MakeRgb(double red, double green, double blue) {
  return {.red = red, .green = green, .blue = blue};
}

[[nodiscard]] constexpr Color Srgba(double red, double green, double blue,
                                    double alpha) {
  const Rgb components = MakeRgb(red, green, blue);
  return {.color_space = ColorSpace::kSrgb,
          .components = components,
          .alpha = alpha,
          .srgb = components};
}

[[nodiscard]] constexpr Color Srgb(double red, double green, double blue) {
  return Srgba(red, green, blue, 1.0);
}

[[nodiscard]] constexpr CubicBezier Bezier(double x1, double y1, double x2,
                                           double y2) {
  return {.x1 = x1, .y1 = y1, .x2 = x2, .y2 = y2};
}

template <typename T>
struct ValueEntry {
  std::string_view name;
  T Tokens::* token;
  std::optional<T> Overrides::* value;
  std::span<const T> defaults;
  unsigned modifiers = 0U;
};

template <typename T>
struct AliasEntry {
  std::string_view name;
  T Tokens::* token;
  std::optional<T> Overrides::* value;
  T Tokens::* target;
};

template <typename T>
struct Deprecation {
  std::optional<T> Overrides::* replacement;
  std::optional<T> Overrides::* deprecated;
};

template <typename T, std::size_t N>
using ValueTable = std::array<ValueEntry<T>, N>;

template <typename T, std::size_t N>
using AliasTable = std::array<AliasEntry<T>, N>;

template <typename T, std::size_t N>
using DeprecationTable = std::array<Deprecation<T>, N>;

[[nodiscard]] constexpr std::size_t Index(const Context& context,
                                          unsigned modifiers) {
  std::size_t index = 0;
  if ((modifiers & kTheme) != 0U) {
    index = (index * 2) + static_cast<std::size_t>(context.theme);
  }
  if ((modifiers & kContrast) != 0U) {
    index = (index * 2) + static_cast<std::size_t>(context.contrast);
  }
  if ((modifiers & kDensity) != 0U) {
    index = (index * 4) + static_cast<std::size_t>(context.density);
  }
  if ((modifiers & kMotion) != 0U) {
    index = (index * 2) + static_cast<std::size_t>(context.motion);
  }
  if ((modifiers & kDynamicRange) != 0U) {
    index = (index * 2) + static_cast<std::size_t>(context.dynamic_range);
  }
  return index;
}

template <typename T, std::size_t N>
constexpr void ResolveValues(const ValueTable<T, N>& entries,
                             const Context& context, const Overrides& overrides,
                             Tokens& tokens) {
  for (const ValueEntry<T>& entry : entries) {
    const T fallback = entry.defaults[Index(context, entry.modifiers)];
    tokens.*entry.token = (overrides.*entry.value).value_or(fallback);
  }
}

template <typename T, std::size_t N>
constexpr void ResolveAliases(const AliasTable<T, N>& entries,
                              const Overrides& overrides, Tokens& tokens) {
  for (const AliasEntry<T>& entry : entries) {
    const T target = tokens.*entry.target;
    tokens.*entry.token = entry.value == nullptr
                              ? target
                              : (overrides.*entry.value).value_or(target);
  }
}

template <typename T, std::size_t N>
constexpr void ApplyDeprecations(const DeprecationTable<T, N>& entries,
                                 Overrides& overrides) {
  for (const Deprecation<T>& entry : entries) {
    if (!(overrides.*entry.replacement).has_value()) {
      overrides.*entry.replacement = overrides.*entry.deprecated;
    }
  }
}

template <typename Entries, typename Visitor>
constexpr void VisitEntries(const Entries& entries, const Tokens& tokens,
                            Visitor& visitor) {
  for (const auto& entry : entries) {
    visitor(entry.name, tokens.*entry.token);
  }
}

inline constexpr std::array<std::string_view, 1> kFontFamily0 = {{
    "system-ui",
}};

inline constexpr std::array<ShadowLayer, 1> kShadow0 = {{
    {
        .color = Srgb(0.0, 0.0, 0.0),
        .offset_x = 0.0,
        .offset_y = 8.0,
        .blur = 24.0,
        .spread = 0.0,
        .inset = false,
    },
}};

inline constexpr std::array<ShadowLayer, 1> kShadow1 = {{
    {
        .color = Srgb(0.0, 0.0, 0.0),
        .offset_x = 0.0,
        .offset_y = 4.0,
        .blur = 12.0,
        .spread = 0.0,
        .inset = false,
    },
}};

inline constexpr std::array<ShadowLayer, 1> kShadow2 = {{
    {
        .color = Srgb(0.0, 0.0, 0.0),
        .offset_x = 0.0,
        .offset_y = 2.0,
        .blur = 6.0,
        .spread = 0.0,
        .inset = false,
    },
}};

struct Defaults {
  std::array<Color, 1> alumbra_token_color_saccent_splayed;
  std::array<Color, 4> alumbra_token_color_sborder_ssubtle;
  std::array<Color, 4> alumbra_token_color_scontrol_sbackground;
  std::array<Color, 4> alumbra_token_color_scontrol_sdisabled;
  std::array<Color, 4> alumbra_token_color_scontrol_sforeground;
  std::array<Color, 4> alumbra_token_color_scontrol_shover;
  std::array<Color, 4> alumbra_token_color_scontrol_spressed;
  std::array<Color, 4> alumbra_token_color_sfocus_sring;
  std::array<Color, 4> alumbra_token_color_ssurface_sbase;
  std::array<Color, 4> alumbra_token_color_ssurface_selevated;
  std::array<Color, 4> alumbra_token_color_ssurface_soverlay;
  std::array<Color, 1> alumbra_token_color_ssurface_sscrim;
  std::array<Color, 1> alumbra_token_color_stext_sdisabled;
  std::array<Color, 4> alumbra_token_color_stext_sprimary;
  std::array<Color, 4> alumbra_token_color_stext_ssecondary;
  std::array<Shadow, 1> alumbra_token_elevation_smodal;
  std::array<Shadow, 1> alumbra_token_elevation_spanel;
  std::array<Shadow, 1> alumbra_token_elevation_sraised;
  std::array<double, 1> alumbra_token_layer_sad_uchrome{};
  std::array<double, 1> alumbra_token_layer_sad_usdk_ucontainer{};
  std::array<double, 1> alumbra_token_layer_scaptions{};
  std::array<double, 1> alumbra_token_layer_scontrols{};
  std::array<double, 1> alumbra_token_layer_svideo_usurface{};
  std::array<double, 2> alumbra_token_luminance_shdr_uhighlight_uceiling{};
  std::array<double, 1> alumbra_token_luminance_sreference_uwhite{};
  std::array<Milliseconds, 1> alumbra_token_motion_sduration_sinstant{};
  std::array<Milliseconds, 2> alumbra_token_motion_sduration_slong{};
  std::array<Milliseconds, 2> alumbra_token_motion_sduration_smedium{};
  std::array<Milliseconds, 2> alumbra_token_motion_sduration_sshort{};
  std::array<CubicBezier, 1> alumbra_token_motion_seasing_semphasized;
  std::array<CubicBezier, 1> alumbra_token_motion_seasing_sstandard;
  std::array<double, 1> alumbra_token_opacity_sdisabled{};
  std::array<Pixels, 1> alumbra_token_radius_slarge{};
  std::array<Pixels, 1> alumbra_token_radius_smedium{};
  std::array<Pixels, 1> alumbra_token_radius_spill{};
  std::array<Pixels, 1> alumbra_token_radius_ssmall{};
  std::array<Pixels, 4> alumbra_token_spacing_sextra_ularge{};
  std::array<Pixels, 4> alumbra_token_spacing_slarge{};
  std::array<Pixels, 4> alumbra_token_spacing_smedium{};
  std::array<Pixels, 1> alumbra_token_spacing_snone{};
  std::array<Pixels, 4> alumbra_token_spacing_ssmall{};
  std::array<Pixels, 4> alumbra_token_spacing_stight{};
  std::array<Pixels, 4> alumbra_token_target_sminimum{};
  std::array<FontFamily, 1> alumbra_token_typography_sfont_ufamily_sui;
  std::array<Pixels, 4> alumbra_token_typography_sfont_usize_sbody{};
  std::array<Pixels, 4> alumbra_token_typography_sfont_usize_scaption{};
  std::array<Pixels, 4> alumbra_token_typography_sfont_usize_sdisplay{};
  std::array<Pixels, 4> alumbra_token_typography_sfont_usize_stitle{};
  std::array<double, 1> alumbra_token_typography_sfont_uweight_sbold{};
  std::array<double, 1> alumbra_token_typography_sfont_uweight_smedium{};
  std::array<double, 1> alumbra_token_typography_sfont_uweight_sregular{};
  std::array<double, 1> alumbra_token_typography_sline_uheight_sbody{};
  std::array<double, 1> alumbra_token_typography_sline_uheight_scompact{};
};

inline constexpr Defaults kDefaults = {
    .alumbra_token_color_saccent_splayed = {{
        Srgb(0.055, 0.647, 0.914),
    }},
    .alumbra_token_color_sborder_ssubtle = {{
        Srgb(0.392, 0.455, 0.545),
        Srgb(1.0, 1.0, 1.0),
        Srgb(0.278, 0.333, 0.412),
        Srgb(0.0, 0.0, 0.0),
    }},
    .alumbra_token_color_scontrol_sbackground = {{
        Srgb(0.118, 0.161, 0.231),
        Srgb(0.0, 0.0, 0.0),
        Srgb(0.886, 0.91, 0.941),
        Srgb(1.0, 1.0, 1.0),
    }},
    .alumbra_token_color_scontrol_sdisabled = {{
        Srgb(0.118, 0.161, 0.231),
        Srgb(0.0, 0.0, 0.0),
        Srgb(0.945, 0.961, 0.976),
        Srgb(1.0, 1.0, 1.0),
    }},
    .alumbra_token_color_scontrol_sforeground = {{
        Srgb(1.0, 1.0, 1.0),
        Srgb(1.0, 1.0, 1.0),
        Srgb(0.059, 0.09, 0.165),
        Srgb(0.0, 0.0, 0.0),
    }},
    .alumbra_token_color_scontrol_shover = {{
        Srgb(0.2, 0.255, 0.333),
        Srgb(0.059, 0.09, 0.165),
        Srgb(0.796, 0.835, 0.882),
        Srgb(0.886, 0.91, 0.941),
    }},
    .alumbra_token_color_scontrol_spressed = {{
        Srgb(0.278, 0.333, 0.412),
        Srgb(0.118, 0.161, 0.231),
        Srgb(0.58, 0.639, 0.722),
        Srgb(0.796, 0.835, 0.882),
    }},
    .alumbra_token_color_sfocus_sring = {{
        Srgb(0.055, 0.647, 0.914),
        Srgb(1.0, 1.0, 0.0),
        Srgb(0.012, 0.412, 0.631),
        Srgb(0.0, 0.0, 0.0),
    }},
    .alumbra_token_color_ssurface_sbase = {{
        Srgb(0.035, 0.055, 0.094),
        Srgb(0.0, 0.0, 0.0),
        Srgb(1.0, 1.0, 1.0),
        Srgb(1.0, 1.0, 1.0),
    }},
    .alumbra_token_color_ssurface_selevated = {{
        Srgb(0.059, 0.09, 0.165),
        Srgb(0.0, 0.0, 0.0),
        Srgb(0.973, 0.98, 0.988),
        Srgb(1.0, 1.0, 1.0),
    }},
    .alumbra_token_color_ssurface_soverlay = {{
        Srgb(0.118, 0.161, 0.231),
        Srgb(0.0, 0.0, 0.0),
        Srgb(0.945, 0.961, 0.976),
        Srgb(1.0, 1.0, 1.0),
    }},
    .alumbra_token_color_ssurface_sscrim = {{
        Srgba(0.0, 0.0, 0.0, 0.72),
    }},
    .alumbra_token_color_stext_sdisabled = {{
        Srgb(0.392, 0.455, 0.545),
    }},
    .alumbra_token_color_stext_sprimary = {{
        Srgb(0.973, 0.98, 0.988),
        Srgb(1.0, 1.0, 1.0),
        Srgb(0.059, 0.09, 0.165),
        Srgb(0.0, 0.0, 0.0),
    }},
    .alumbra_token_color_stext_ssecondary = {{
        Srgb(0.796, 0.835, 0.882),
        Srgb(1.0, 1.0, 1.0),
        Srgb(0.2, 0.255, 0.333),
        Srgb(0.0, 0.0, 0.0),
    }},
    .alumbra_token_elevation_smodal = {{
        Shadow(kShadow0),
    }},
    .alumbra_token_elevation_spanel = {{
        Shadow(kShadow1),
    }},
    .alumbra_token_elevation_sraised = {{
        Shadow(kShadow2),
    }},
    .alumbra_token_layer_sad_uchrome = {{
        30.0,
    }},
    .alumbra_token_layer_sad_usdk_ucontainer = {{
        40.0,
    }},
    .alumbra_token_layer_scaptions = {{
        10.0,
    }},
    .alumbra_token_layer_scontrols = {{
        20.0,
    }},
    .alumbra_token_layer_svideo_usurface = {{
        0.0,
    }},
    .alumbra_token_luminance_shdr_uhighlight_uceiling = {{
        203.0,
        600.0,
    }},
    .alumbra_token_luminance_sreference_uwhite = {{
        203.0,
    }},
    .alumbra_token_motion_sduration_sinstant = {{
        Milliseconds(0.0),
    }},
    .alumbra_token_motion_sduration_slong = {{
        Milliseconds(320.0),
        Milliseconds(0.0),
    }},
    .alumbra_token_motion_sduration_smedium = {{
        Milliseconds(200.0),
        Milliseconds(0.0),
    }},
    .alumbra_token_motion_sduration_sshort = {{
        Milliseconds(120.0),
        Milliseconds(0.0),
    }},
    .alumbra_token_motion_seasing_semphasized = {{
        Bezier(0.2, 0.0, 0.0, 1.4),
    }},
    .alumbra_token_motion_seasing_sstandard = {{
        Bezier(0.2, 0.0, 0.0, 1.0),
    }},
    .alumbra_token_opacity_sdisabled = {{
        0.48,
    }},
    .alumbra_token_radius_slarge = {{
        16.0,
    }},
    .alumbra_token_radius_smedium = {{
        8.0,
    }},
    .alumbra_token_radius_spill = {{
        999.0,
    }},
    .alumbra_token_radius_ssmall = {{
        4.0,
    }},
    .alumbra_token_spacing_sextra_ularge = {{
        28.0,
        36.0,
        32.0,
        56.0,
    }},
    .alumbra_token_spacing_slarge = {{
        20.0,
        28.0,
        24.0,
        40.0,
    }},
    .alumbra_token_spacing_smedium = {{
        12.0,
        20.0,
        16.0,
        28.0,
    }},
    .alumbra_token_spacing_snone = {{
        0.0,
    }},
    .alumbra_token_spacing_ssmall = {{
        8.0,
        8.0,
        8.0,
        16.0,
    }},
    .alumbra_token_spacing_stight = {{
        4.0,
        4.0,
        4.0,
        8.0,
    }},
    .alumbra_token_target_sminimum = {{
        48.0,
        48.0,
        44.0,
        72.0,
    }},
    .alumbra_token_typography_sfont_ufamily_sui = {{
        FontFamily(kFontFamily0),
    }},
    .alumbra_token_typography_sfont_usize_sbody = {{
        17.0,
        18.0,
        16.0,
        24.0,
    }},
    .alumbra_token_typography_sfont_usize_scaption = {{
        13.0,
        14.0,
        12.0,
        18.0,
    }},
    .alumbra_token_typography_sfont_usize_sdisplay = {{
        34.0,
        36.0,
        32.0,
        52.0,
    }},
    .alumbra_token_typography_sfont_usize_stitle = {{
        25.0,
        26.0,
        24.0,
        36.0,
    }},
    .alumbra_token_typography_sfont_uweight_sbold = {{
        700.0,
    }},
    .alumbra_token_typography_sfont_uweight_smedium = {{
        500.0,
    }},
    .alumbra_token_typography_sfont_uweight_sregular = {{
        400.0,
    }},
    .alumbra_token_typography_sline_uheight_sbody = {{
        1.5,
    }},
    .alumbra_token_typography_sline_uheight_scompact = {{
        1.2,
    }},
};

inline constexpr ValueTable<Color, 15> kColorValues = {{
    {
        .name = "color.accent.played",
        .token = &Tokens::alumbra_token_color_saccent_splayed,
        .value = &Overrides::alumbra_token_color_saccent_splayed,
        .defaults = kDefaults.alumbra_token_color_saccent_splayed,
        .modifiers = 0U,
    },
    {
        .name = "color.border.subtle",
        .token = &Tokens::alumbra_token_color_sborder_ssubtle,
        .value = &Overrides::alumbra_token_color_sborder_ssubtle,
        .defaults = kDefaults.alumbra_token_color_sborder_ssubtle,
        .modifiers = kTheme | kContrast,
    },
    {
        .name = "color.control.background",
        .token = &Tokens::alumbra_token_color_scontrol_sbackground,
        .value = &Overrides::alumbra_token_color_scontrol_sbackground,
        .defaults = kDefaults.alumbra_token_color_scontrol_sbackground,
        .modifiers = kTheme | kContrast,
    },
    {
        .name = "color.control.disabled",
        .token = &Tokens::alumbra_token_color_scontrol_sdisabled,
        .value = &Overrides::alumbra_token_color_scontrol_sdisabled,
        .defaults = kDefaults.alumbra_token_color_scontrol_sdisabled,
        .modifiers = kTheme | kContrast,
    },
    {
        .name = "color.control.foreground",
        .token = &Tokens::alumbra_token_color_scontrol_sforeground,
        .value = &Overrides::alumbra_token_color_scontrol_sforeground,
        .defaults = kDefaults.alumbra_token_color_scontrol_sforeground,
        .modifiers = kTheme | kContrast,
    },
    {
        .name = "color.control.hover",
        .token = &Tokens::alumbra_token_color_scontrol_shover,
        .value = &Overrides::alumbra_token_color_scontrol_shover,
        .defaults = kDefaults.alumbra_token_color_scontrol_shover,
        .modifiers = kTheme | kContrast,
    },
    {
        .name = "color.control.pressed",
        .token = &Tokens::alumbra_token_color_scontrol_spressed,
        .value = &Overrides::alumbra_token_color_scontrol_spressed,
        .defaults = kDefaults.alumbra_token_color_scontrol_spressed,
        .modifiers = kTheme | kContrast,
    },
    {
        .name = "color.focus.ring",
        .token = &Tokens::alumbra_token_color_sfocus_sring,
        .value = &Overrides::alumbra_token_color_sfocus_sring,
        .defaults = kDefaults.alumbra_token_color_sfocus_sring,
        .modifiers = kTheme | kContrast,
    },
    {
        .name = "color.surface.base",
        .token = &Tokens::alumbra_token_color_ssurface_sbase,
        .value = &Overrides::alumbra_token_color_ssurface_sbase,
        .defaults = kDefaults.alumbra_token_color_ssurface_sbase,
        .modifiers = kTheme | kContrast,
    },
    {
        .name = "color.surface.elevated",
        .token = &Tokens::alumbra_token_color_ssurface_selevated,
        .value = &Overrides::alumbra_token_color_ssurface_selevated,
        .defaults = kDefaults.alumbra_token_color_ssurface_selevated,
        .modifiers = kTheme | kContrast,
    },
    {
        .name = "color.surface.overlay",
        .token = &Tokens::alumbra_token_color_ssurface_soverlay,
        .value = &Overrides::alumbra_token_color_ssurface_soverlay,
        .defaults = kDefaults.alumbra_token_color_ssurface_soverlay,
        .modifiers = kTheme | kContrast,
    },
    {
        .name = "color.surface.scrim",
        .token = &Tokens::alumbra_token_color_ssurface_sscrim,
        .value = &Overrides::alumbra_token_color_ssurface_sscrim,
        .defaults = kDefaults.alumbra_token_color_ssurface_sscrim,
        .modifiers = 0U,
    },
    {
        .name = "color.text.disabled",
        .token = &Tokens::alumbra_token_color_stext_sdisabled,
        .value = &Overrides::alumbra_token_color_stext_sdisabled,
        .defaults = kDefaults.alumbra_token_color_stext_sdisabled,
        .modifiers = 0U,
    },
    {
        .name = "color.text.primary",
        .token = &Tokens::alumbra_token_color_stext_sprimary,
        .value = &Overrides::alumbra_token_color_stext_sprimary,
        .defaults = kDefaults.alumbra_token_color_stext_sprimary,
        .modifiers = kTheme | kContrast,
    },
    {
        .name = "color.text.secondary",
        .token = &Tokens::alumbra_token_color_stext_ssecondary,
        .value = &Overrides::alumbra_token_color_stext_ssecondary,
        .defaults = kDefaults.alumbra_token_color_stext_ssecondary,
        .modifiers = kTheme | kContrast,
    },
}};

inline constexpr AliasTable<Color, 18> kColorAliases = {{
    {
        .name = "player.ad.band.background",
        .token = &Tokens::alumbra_token_player_sad_sband_sbackground,
        .value = &Overrides::alumbra_token_player_sad_sband_sbackground,
        .target = &Tokens::alumbra_token_color_ssurface_selevated,
    },
    {
        .name = "player.ad_band.background",
        .token = &Tokens::alumbra_token_player_sad_uband_sbackground,
        .value = &Overrides::alumbra_token_player_sad_uband_sbackground,
        .target = &Tokens::alumbra_token_color_ssurface_soverlay,
    },
    {
        .name = "player.ad_chrome.badge.background",
        .token = &Tokens::alumbra_token_player_sad_uchrome_sbadge_sbackground,
        .value =
            &Overrides::alumbra_token_player_sad_uchrome_sbadge_sbackground,
        .target = &Tokens::alumbra_token_color_scontrol_sbackground,
    },
    {
        .name = "player.button.background",
        .token = &Tokens::alumbra_token_player_sbutton_sbackground,
        .value = &Overrides::alumbra_token_player_sbutton_sbackground,
        .target = &Tokens::alumbra_token_color_scontrol_sbackground,
    },
    {
        .name = "player.button.background.disabled",
        .token = &Tokens::alumbra_token_player_sbutton_sbackground_sdisabled,
        .value = &Overrides::alumbra_token_player_sbutton_sbackground_sdisabled,
        .target = &Tokens::alumbra_token_color_scontrol_sdisabled,
    },
    {
        .name = "player.button.background.focus",
        .token = &Tokens::alumbra_token_player_sbutton_sbackground_sfocus,
        .value = &Overrides::alumbra_token_player_sbutton_sbackground_sfocus,
        .target = &Tokens::alumbra_token_color_scontrol_sbackground,
    },
    {
        .name = "player.button.background.hover",
        .token = &Tokens::alumbra_token_player_sbutton_sbackground_shover,
        .value = &Overrides::alumbra_token_player_sbutton_sbackground_shover,
        .target = &Tokens::alumbra_token_color_scontrol_shover,
    },
    {
        .name = "player.button.background.pressed",
        .token = &Tokens::alumbra_token_player_sbutton_sbackground_spressed,
        .value = &Overrides::alumbra_token_player_sbutton_sbackground_spressed,
        .target = &Tokens::alumbra_token_color_scontrol_spressed,
    },
    {
        .name = "player.button.foreground",
        .token = &Tokens::alumbra_token_player_sbutton_sforeground,
        .value = &Overrides::alumbra_token_player_sbutton_sforeground,
        .target = &Tokens::alumbra_token_color_scontrol_sforeground,
    },
    {
        .name = "player.button.foreground.disabled",
        .token = &Tokens::alumbra_token_player_sbutton_sforeground_sdisabled,
        .value = &Overrides::alumbra_token_player_sbutton_sforeground_sdisabled,
        .target = &Tokens::alumbra_token_color_stext_sdisabled,
    },
    {
        .name = "player.button.foreground.focus",
        .token = &Tokens::alumbra_token_player_sbutton_sforeground_sfocus,
        .value = &Overrides::alumbra_token_player_sbutton_sforeground_sfocus,
        .target = &Tokens::alumbra_token_color_scontrol_sforeground,
    },
    {
        .name = "player.button.foreground.hover",
        .token = &Tokens::alumbra_token_player_sbutton_sforeground_shover,
        .value = &Overrides::alumbra_token_player_sbutton_sforeground_shover,
        .target = &Tokens::alumbra_token_color_scontrol_sforeground,
    },
    {
        .name = "player.button.foreground.pressed",
        .token = &Tokens::alumbra_token_player_sbutton_sforeground_spressed,
        .value = &Overrides::alumbra_token_player_sbutton_sforeground_spressed,
        .target = &Tokens::alumbra_token_color_scontrol_sforeground,
    },
    {
        .name = "player.captions.text",
        .token = &Tokens::alumbra_token_player_scaptions_stext,
        .value = &Overrides::alumbra_token_player_scaptions_stext,
        .target = &Tokens::alumbra_token_color_stext_sprimary,
    },
    {
        .name = "player.captions.window",
        .token = &Tokens::alumbra_token_player_scaptions_swindow,
        .value = &Overrides::alumbra_token_player_scaptions_swindow,
        .target = &Tokens::alumbra_token_color_ssurface_sscrim,
    },
    {
        .name = "player.focus.ring",
        .token = &Tokens::alumbra_token_player_sfocus_sring,
        .value = &Overrides::alumbra_token_player_sfocus_sring,
        .target = &Tokens::alumbra_token_color_sfocus_sring,
    },
    {
        .name = "player.seekbar.played",
        .token = &Tokens::alumbra_token_player_sseekbar_splayed,
        .value = &Overrides::alumbra_token_player_sseekbar_splayed,
        .target = &Tokens::alumbra_token_color_saccent_splayed,
    },
    {
        .name = "player.seekbar.track",
        .token = &Tokens::alumbra_token_player_sseekbar_strack,
        .value = &Overrides::alumbra_token_player_sseekbar_strack,
        .target = &Tokens::alumbra_token_color_sborder_ssubtle,
    },
}};

inline constexpr ValueTable<double, 28> kNumberValues = {{
    {
        .name = "layer.ad_chrome",
        .token = &Tokens::alumbra_token_layer_sad_uchrome,
        .value = &Overrides::alumbra_token_layer_sad_uchrome,
        .defaults = kDefaults.alumbra_token_layer_sad_uchrome,
        .modifiers = 0U,
    },
    {
        .name = "layer.ad_sdk_container",
        .token = &Tokens::alumbra_token_layer_sad_usdk_ucontainer,
        .value = &Overrides::alumbra_token_layer_sad_usdk_ucontainer,
        .defaults = kDefaults.alumbra_token_layer_sad_usdk_ucontainer,
        .modifiers = 0U,
    },
    {
        .name = "layer.captions",
        .token = &Tokens::alumbra_token_layer_scaptions,
        .value = &Overrides::alumbra_token_layer_scaptions,
        .defaults = kDefaults.alumbra_token_layer_scaptions,
        .modifiers = 0U,
    },
    {
        .name = "layer.controls",
        .token = &Tokens::alumbra_token_layer_scontrols,
        .value = &Overrides::alumbra_token_layer_scontrols,
        .defaults = kDefaults.alumbra_token_layer_scontrols,
        .modifiers = 0U,
    },
    {
        .name = "layer.video_surface",
        .token = &Tokens::alumbra_token_layer_svideo_usurface,
        .value = &Overrides::alumbra_token_layer_svideo_usurface,
        .defaults = kDefaults.alumbra_token_layer_svideo_usurface,
        .modifiers = 0U,
    },
    {
        .name = "luminance.hdr_highlight_ceiling",
        .token = &Tokens::alumbra_token_luminance_shdr_uhighlight_uceiling,
        .value = &Overrides::alumbra_token_luminance_shdr_uhighlight_uceiling,
        .defaults = kDefaults.alumbra_token_luminance_shdr_uhighlight_uceiling,
        .modifiers = kDynamicRange,
    },
    {
        .name = "luminance.reference_white",
        .token = &Tokens::alumbra_token_luminance_sreference_uwhite,
        .value = &Overrides::alumbra_token_luminance_sreference_uwhite,
        .defaults = kDefaults.alumbra_token_luminance_sreference_uwhite,
        .modifiers = 0U,
    },
    {
        .name = "opacity.disabled",
        .token = &Tokens::alumbra_token_opacity_sdisabled,
        .value = &Overrides::alumbra_token_opacity_sdisabled,
        .defaults = kDefaults.alumbra_token_opacity_sdisabled,
        .modifiers = 0U,
    },
    {
        .name = "radius.large",
        .token = &Tokens::alumbra_token_radius_slarge,
        .value = &Overrides::alumbra_token_radius_slarge,
        .defaults = kDefaults.alumbra_token_radius_slarge,
        .modifiers = 0U,
    },
    {
        .name = "radius.medium",
        .token = &Tokens::alumbra_token_radius_smedium,
        .value = &Overrides::alumbra_token_radius_smedium,
        .defaults = kDefaults.alumbra_token_radius_smedium,
        .modifiers = 0U,
    },
    {
        .name = "radius.pill",
        .token = &Tokens::alumbra_token_radius_spill,
        .value = &Overrides::alumbra_token_radius_spill,
        .defaults = kDefaults.alumbra_token_radius_spill,
        .modifiers = 0U,
    },
    {
        .name = "radius.small",
        .token = &Tokens::alumbra_token_radius_ssmall,
        .value = &Overrides::alumbra_token_radius_ssmall,
        .defaults = kDefaults.alumbra_token_radius_ssmall,
        .modifiers = 0U,
    },
    {
        .name = "spacing.extra_large",
        .token = &Tokens::alumbra_token_spacing_sextra_ularge,
        .value = &Overrides::alumbra_token_spacing_sextra_ularge,
        .defaults = kDefaults.alumbra_token_spacing_sextra_ularge,
        .modifiers = kDensity,
    },
    {
        .name = "spacing.large",
        .token = &Tokens::alumbra_token_spacing_slarge,
        .value = &Overrides::alumbra_token_spacing_slarge,
        .defaults = kDefaults.alumbra_token_spacing_slarge,
        .modifiers = kDensity,
    },
    {
        .name = "spacing.medium",
        .token = &Tokens::alumbra_token_spacing_smedium,
        .value = &Overrides::alumbra_token_spacing_smedium,
        .defaults = kDefaults.alumbra_token_spacing_smedium,
        .modifiers = kDensity,
    },
    {
        .name = "spacing.none",
        .token = &Tokens::alumbra_token_spacing_snone,
        .value = &Overrides::alumbra_token_spacing_snone,
        .defaults = kDefaults.alumbra_token_spacing_snone,
        .modifiers = 0U,
    },
    {
        .name = "spacing.small",
        .token = &Tokens::alumbra_token_spacing_ssmall,
        .value = &Overrides::alumbra_token_spacing_ssmall,
        .defaults = kDefaults.alumbra_token_spacing_ssmall,
        .modifiers = kDensity,
    },
    {
        .name = "spacing.tight",
        .token = &Tokens::alumbra_token_spacing_stight,
        .value = &Overrides::alumbra_token_spacing_stight,
        .defaults = kDefaults.alumbra_token_spacing_stight,
        .modifiers = kDensity,
    },
    {
        .name = "target.minimum",
        .token = &Tokens::alumbra_token_target_sminimum,
        .value = &Overrides::alumbra_token_target_sminimum,
        .defaults = kDefaults.alumbra_token_target_sminimum,
        .modifiers = kDensity,
    },
    {
        .name = "typography.font_size.body",
        .token = &Tokens::alumbra_token_typography_sfont_usize_sbody,
        .value = &Overrides::alumbra_token_typography_sfont_usize_sbody,
        .defaults = kDefaults.alumbra_token_typography_sfont_usize_sbody,
        .modifiers = kDensity,
    },
    {
        .name = "typography.font_size.caption",
        .token = &Tokens::alumbra_token_typography_sfont_usize_scaption,
        .value = &Overrides::alumbra_token_typography_sfont_usize_scaption,
        .defaults = kDefaults.alumbra_token_typography_sfont_usize_scaption,
        .modifiers = kDensity,
    },
    {
        .name = "typography.font_size.display",
        .token = &Tokens::alumbra_token_typography_sfont_usize_sdisplay,
        .value = &Overrides::alumbra_token_typography_sfont_usize_sdisplay,
        .defaults = kDefaults.alumbra_token_typography_sfont_usize_sdisplay,
        .modifiers = kDensity,
    },
    {
        .name = "typography.font_size.title",
        .token = &Tokens::alumbra_token_typography_sfont_usize_stitle,
        .value = &Overrides::alumbra_token_typography_sfont_usize_stitle,
        .defaults = kDefaults.alumbra_token_typography_sfont_usize_stitle,
        .modifiers = kDensity,
    },
    {
        .name = "typography.font_weight.bold",
        .token = &Tokens::alumbra_token_typography_sfont_uweight_sbold,
        .value = &Overrides::alumbra_token_typography_sfont_uweight_sbold,
        .defaults = kDefaults.alumbra_token_typography_sfont_uweight_sbold,
        .modifiers = 0U,
    },
    {
        .name = "typography.font_weight.medium",
        .token = &Tokens::alumbra_token_typography_sfont_uweight_smedium,
        .value = &Overrides::alumbra_token_typography_sfont_uweight_smedium,
        .defaults = kDefaults.alumbra_token_typography_sfont_uweight_smedium,
        .modifiers = 0U,
    },
    {
        .name = "typography.font_weight.regular",
        .token = &Tokens::alumbra_token_typography_sfont_uweight_sregular,
        .value = &Overrides::alumbra_token_typography_sfont_uweight_sregular,
        .defaults = kDefaults.alumbra_token_typography_sfont_uweight_sregular,
        .modifiers = 0U,
    },
    {
        .name = "typography.line_height.body",
        .token = &Tokens::alumbra_token_typography_sline_uheight_sbody,
        .value = &Overrides::alumbra_token_typography_sline_uheight_sbody,
        .defaults = kDefaults.alumbra_token_typography_sline_uheight_sbody,
        .modifiers = 0U,
    },
    {
        .name = "typography.line_height.compact",
        .token = &Tokens::alumbra_token_typography_sline_uheight_scompact,
        .value = &Overrides::alumbra_token_typography_sline_uheight_scompact,
        .defaults = kDefaults.alumbra_token_typography_sline_uheight_scompact,
        .modifiers = 0U,
    },
}};

inline constexpr AliasTable<double, 2> kNumberAliases = {{
    {
        .name = "player.button.radius",
        .token = &Tokens::alumbra_token_player_sbutton_sradius,
        .value = &Overrides::alumbra_token_player_sbutton_sradius,
        .target = &Tokens::alumbra_token_radius_smedium,
    },
    {
        .name = "player.captions.font_size",
        .token = &Tokens::alumbra_token_player_scaptions_sfont_usize,
        .value = &Overrides::alumbra_token_player_scaptions_sfont_usize,
        .target = &Tokens::alumbra_token_typography_sfont_usize_scaption,
    },
}};

inline constexpr ValueTable<Milliseconds, 4> kDurationValues = {{
    {
        .name = "motion.duration.instant",
        .token = &Tokens::alumbra_token_motion_sduration_sinstant,
        .value = &Overrides::alumbra_token_motion_sduration_sinstant,
        .defaults = kDefaults.alumbra_token_motion_sduration_sinstant,
        .modifiers = 0U,
    },
    {
        .name = "motion.duration.long",
        .token = &Tokens::alumbra_token_motion_sduration_slong,
        .value = &Overrides::alumbra_token_motion_sduration_slong,
        .defaults = kDefaults.alumbra_token_motion_sduration_slong,
        .modifiers = kMotion,
    },
    {
        .name = "motion.duration.medium",
        .token = &Tokens::alumbra_token_motion_sduration_smedium,
        .value = &Overrides::alumbra_token_motion_sduration_smedium,
        .defaults = kDefaults.alumbra_token_motion_sduration_smedium,
        .modifiers = kMotion,
    },
    {
        .name = "motion.duration.short",
        .token = &Tokens::alumbra_token_motion_sduration_sshort,
        .value = &Overrides::alumbra_token_motion_sduration_sshort,
        .defaults = kDefaults.alumbra_token_motion_sduration_sshort,
        .modifiers = kMotion,
    },
}};

inline constexpr ValueTable<FontFamily, 1> kFontFamilyValues = {{
    {
        .name = "typography.font_family.ui",
        .token = &Tokens::alumbra_token_typography_sfont_ufamily_sui,
        .value = &Overrides::alumbra_token_typography_sfont_ufamily_sui,
        .defaults = kDefaults.alumbra_token_typography_sfont_ufamily_sui,
        .modifiers = 0U,
    },
}};

inline constexpr ValueTable<CubicBezier, 2> kCubicBezierValues = {{
    {
        .name = "motion.easing.emphasized",
        .token = &Tokens::alumbra_token_motion_seasing_semphasized,
        .value = &Overrides::alumbra_token_motion_seasing_semphasized,
        .defaults = kDefaults.alumbra_token_motion_seasing_semphasized,
        .modifiers = 0U,
    },
    {
        .name = "motion.easing.standard",
        .token = &Tokens::alumbra_token_motion_seasing_sstandard,
        .value = &Overrides::alumbra_token_motion_seasing_sstandard,
        .defaults = kDefaults.alumbra_token_motion_seasing_sstandard,
        .modifiers = 0U,
    },
}};

inline constexpr ValueTable<Shadow, 3> kShadowValues = {{
    {
        .name = "elevation.modal",
        .token = &Tokens::alumbra_token_elevation_smodal,
        .value = &Overrides::alumbra_token_elevation_smodal,
        .defaults = kDefaults.alumbra_token_elevation_smodal,
        .modifiers = 0U,
    },
    {
        .name = "elevation.panel",
        .token = &Tokens::alumbra_token_elevation_spanel,
        .value = &Overrides::alumbra_token_elevation_spanel,
        .defaults = kDefaults.alumbra_token_elevation_spanel,
        .modifiers = 0U,
    },
    {
        .name = "elevation.raised",
        .token = &Tokens::alumbra_token_elevation_sraised,
        .value = &Overrides::alumbra_token_elevation_sraised,
        .defaults = kDefaults.alumbra_token_elevation_sraised,
        .modifiers = 0U,
    },
}};

[[nodiscard]] constexpr Tokens ResolveAll(const Context& context,
                                          const Overrides& overrides) {
  Tokens tokens;
  ResolveValues(kColorValues, context, overrides, tokens);
  ResolveValues(kNumberValues, context, overrides, tokens);
  ResolveValues(kDurationValues, context, overrides, tokens);
  ResolveValues(kFontFamilyValues, context, overrides, tokens);
  ResolveValues(kCubicBezierValues, context, overrides, tokens);
  ResolveValues(kShadowValues, context, overrides, tokens);
  ResolveAliases(kColorAliases, overrides, tokens);
  ResolveAliases(kNumberAliases, overrides, tokens);
  return tokens;
}

template <typename Visitor>
constexpr void VisitAll(const Tokens& tokens, Visitor& visitor) {
  VisitEntries(kColorValues, tokens, visitor);
  VisitEntries(kColorAliases, tokens, visitor);
  VisitEntries(kNumberValues, tokens, visitor);
  VisitEntries(kNumberAliases, tokens, visitor);
  VisitEntries(kDurationValues, tokens, visitor);
  VisitEntries(kFontFamilyValues, tokens, visitor);
  VisitEntries(kCubicBezierValues, tokens, visitor);
  VisitEntries(kShadowValues, tokens, visitor);
}

}

[[nodiscard]] constexpr Tokens Resolve(const Context& context = {},
                                       const Overrides& overrides = {}) {
  return internal::ResolveAll(context, overrides);
}

template <typename Visitor>
constexpr void VisitTokens(const Tokens& tokens, Visitor& visitor) {
  internal::VisitAll(tokens, visitor);
}

}

#endif
