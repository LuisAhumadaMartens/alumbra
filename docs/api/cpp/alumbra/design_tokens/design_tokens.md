\file alumbra/design_tokens/design_tokens.h
Design tokens as constexpr values, resolved for a context.

Generated from design/tokens/ by `python3 tools/design/generate_tokens.py`; do not edit it by hand.

\namespace alumbra
Alumbra's top-level namespace for design tokens.

\namespace alumbra::design_tokens
Design tokens for C++ renderers: contexts, value types and the resolved token set.

Resolve() evaluates every token for a Context, applying aliases and optional Overrides. The
[design token reference](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/reference/design-tokens.md)
lists every token, its type and its default.

\enum alumbra::design_tokens::Theme
Values of the theme modifier of a Context.

\var alumbra::design_tokens::Theme::kDark
The dark context. This is the default.

\var alumbra::design_tokens::Theme::kLight
The light context.

\enum alumbra::design_tokens::Contrast
Values of the contrast modifier of a Context.

\var alumbra::design_tokens::Contrast::kNormal
The normal context. This is the default.

\var alumbra::design_tokens::Contrast::kHigh
The high context.

\enum alumbra::design_tokens::Density
Values of the density modifier of a Context.

\var alumbra::design_tokens::Density::kPhone
The phone context.

\var alumbra::design_tokens::Density::kTablet
The tablet context.

\var alumbra::design_tokens::Density::kDesktop
The desktop context. This is the default.

\var alumbra::design_tokens::Density::kTv
The tv context.

\enum alumbra::design_tokens::Motion
Values of the motion modifier of a Context.

\var alumbra::design_tokens::Motion::kFull
The full context. This is the default.

\var alumbra::design_tokens::Motion::kReduced
The reduced context.

\enum alumbra::design_tokens::DynamicRange
Values of the dynamic-range modifier of a Context.

\var alumbra::design_tokens::DynamicRange::kSdr
The sdr context. This is the default.

\var alumbra::design_tokens::DynamicRange::kHdr
The hdr context.

\struct alumbra::design_tokens::Context
One value of every modifier, which selects the resolved value of each token.

\var Theme alumbra::design_tokens::Context::theme
The theme context; dark by default.

\var Contrast alumbra::design_tokens::Context::contrast
The contrast context; normal by default.

\var Density alumbra::design_tokens::Context::density
The density context; desktop by default.

\var Motion alumbra::design_tokens::Context::motion
The motion context; full by default.

\var DynamicRange alumbra::design_tokens::Context::dynamic_range
The dynamic-range context; sdr by default.

\var alumbra::design_tokens::kThemeValues
Every Theme value, in declaration order.

\fn std::string_view alumbra::design_tokens::Name(Theme value)
Returns the name of a Theme value as written in the token files and the CSS selectors.

\var alumbra::design_tokens::kContrastValues
Every Contrast value, in declaration order.

\fn std::string_view alumbra::design_tokens::Name(Contrast value)
Returns the name of a Contrast value as written in the token files and the CSS selectors.

\var alumbra::design_tokens::kDensityValues
Every Density value, in declaration order.

\fn std::string_view alumbra::design_tokens::Name(Density value)
Returns the name of a Density value as written in the token files and the CSS selectors.

\var alumbra::design_tokens::kMotionValues
Every Motion value, in declaration order.

\fn std::string_view alumbra::design_tokens::Name(Motion value)
Returns the name of a Motion value as written in the token files and the CSS selectors.

\var alumbra::design_tokens::kDynamicRangeValues
Every DynamicRange value, in declaration order.

\fn std::string_view alumbra::design_tokens::Name(DynamicRange value)
Returns the name of a DynamicRange value as written in the token files and the CSS selectors.

\enum alumbra::design_tokens::ColorSpace
Colour space of a Color.

\var alumbra::design_tokens::ColorSpace::kSrgb
sRGB, the baseline colour space.

\var alumbra::design_tokens::ColorSpace::kDisplayP3
Display P3, a wide-gamut space.

\var alumbra::design_tokens::ColorSpace::kRec2020
Rec. 2020, a wide-gamut space.

\struct alumbra::design_tokens::Rgb
Red, green and blue components of a colour.

\var double alumbra::design_tokens::Rgb::red
Red component, from 0 to 1.

\var double alumbra::design_tokens::Rgb::green
Green component, from 0 to 1.

\var double alumbra::design_tokens::Rgb::blue
Blue component, from 0 to 1.

\struct alumbra::design_tokens::Color
Colour with its colour space, alpha and sRGB fallback.

\var ColorSpace alumbra::design_tokens::Color::color_space
Colour space of components.

\var Rgb alumbra::design_tokens::Color::components
Components in color_space.

\var double alumbra::design_tokens::Color::alpha
Opacity, from 0 to 1.

\var Rgb alumbra::design_tokens::Color::srgb
The same colour converted to sRGB, for targets without wide gamut.

\struct alumbra::design_tokens::CubicBezier
Control points of a cubic Bezier easing curve.

\var double alumbra::design_tokens::CubicBezier::x1
Abscissa of the first control point.

\var double alumbra::design_tokens::CubicBezier::y1
Ordinate of the first control point.

\var double alumbra::design_tokens::CubicBezier::x2
Abscissa of the second control point.

\var double alumbra::design_tokens::CubicBezier::y2
Ordinate of the second control point.

\struct alumbra::design_tokens::ShadowLayer
One layer of a box shadow.

\var Color alumbra::design_tokens::ShadowLayer::color
Shadow colour.

\var double alumbra::design_tokens::ShadowLayer::offset_x
Horizontal offset in pixels.

\var double alumbra::design_tokens::ShadowLayer::offset_y
Vertical offset in pixels.

\var double alumbra::design_tokens::ShadowLayer::blur
Blur radius in pixels.

\var double alumbra::design_tokens::ShadowLayer::spread
Spread radius in pixels.

\var bool alumbra::design_tokens::ShadowLayer::inset
Whether the shadow is drawn inside the box.

\typedef alumbra::design_tokens::Pixels
Length in CSS pixels.

\typedef alumbra::design_tokens::Milliseconds
Duration in milliseconds as a double.

\typedef alumbra::design_tokens::FontFamily
Ordered font family names, most preferred first.

\typedef alumbra::design_tokens::Shadow
Shadow layers, outermost first.

\struct alumbra::design_tokens::Tokens
Resolved value of every public token for one context.

Member names are the token identifiers; the token reference maps each one to its token name.

\var Color alumbra::design_tokens::Tokens::alumbra_token_color_saccent_splayed
Accent color for the played seekbar track.

\var Color alumbra::design_tokens::Tokens::alumbra_token_color_sborder_ssubtle
Subtle border for controls and panels.

\var Color alumbra::design_tokens::Tokens::alumbra_token_color_scontrol_sbackground
Default interactive control surface.

\var Color alumbra::design_tokens::Tokens::alumbra_token_color_scontrol_sdisabled
Disabled control surface.

\var Color alumbra::design_tokens::Tokens::alumbra_token_color_scontrol_sforeground
Control label and icon color on control.background.

\var Color alumbra::design_tokens::Tokens::alumbra_token_color_scontrol_shover
Hovered or D-pad highlighted control surface.

\var Color alumbra::design_tokens::Tokens::alumbra_token_color_scontrol_spressed
Pressed control surface.

\var Color alumbra::design_tokens::Tokens::alumbra_token_color_sfocus_sring
Visible keyboard and D-pad focus indicator on base and elevated surfaces.

\var Color alumbra::design_tokens::Tokens::alumbra_token_color_ssurface_sbase
Primary player surface.

\var Color alumbra::design_tokens::Tokens::alumbra_token_color_ssurface_selevated
Raised control and panel surface.

\var Color alumbra::design_tokens::Tokens::alumbra_token_color_ssurface_soverlay
Top-level player overlay surface.

\var Color alumbra::design_tokens::Tokens::alumbra_token_color_ssurface_sscrim
Translucent scrim composited over the current video frame.

\var Color alumbra::design_tokens::Tokens::alumbra_token_color_stext_sdisabled
Disabled text; excluded from the declared WCAG text pairs because disabled controls are exempt.

\var Color alumbra::design_tokens::Tokens::alumbra_token_color_stext_sprimary
Primary text on base and elevated surfaces.

\var Color alumbra::design_tokens::Tokens::alumbra_token_color_stext_ssecondary
Secondary text on base and elevated surfaces.

\var Shadow alumbra::design_tokens::Tokens::alumbra_token_elevation_smodal
Modal overlay elevation.

\var Shadow alumbra::design_tokens::Tokens::alumbra_token_elevation_spanel
Panel elevation.

\var Shadow alumbra::design_tokens::Tokens::alumbra_token_elevation_sraised
Raised control elevation.

\var double alumbra::design_tokens::Tokens::alumbra_token_layer_sad_uchrome
Player-owned ad chrome above controls and below the ad SDK container.

\var double alumbra::design_tokens::Tokens::alumbra_token_layer_sad_usdk_ucontainer
Ad SDK container owns input during ad playback.

\var double alumbra::design_tokens::Tokens::alumbra_token_layer_scaptions
Text tracks above video and below player controls.

\var double alumbra::design_tokens::Tokens::alumbra_token_layer_scontrols
Player controls above text tracks.

\var double alumbra::design_tokens::Tokens::alumbra_token_layer_svideo_usurface
Bottom video or ad surface in the overlay stack.

\var double alumbra::design_tokens::Tokens::alumbra_token_luminance_shdr_uhighlight_uceiling
HDR highlight headroom in cd/m2; renderer mapping is staged.

\var double alumbra::design_tokens::Tokens::alumbra_token_luminance_sreference_uwhite
HDR UI reference white in cd/m2.

\var Milliseconds alumbra::design_tokens::Tokens::alumbra_token_motion_sduration_sinstant
No animation.

\var Milliseconds alumbra::design_tokens::Tokens::alumbra_token_motion_sduration_slong
Long emphasis transition.

\var Milliseconds alumbra::design_tokens::Tokens::alumbra_token_motion_sduration_smedium
Standard state transition.

\var Milliseconds alumbra::design_tokens::Tokens::alumbra_token_motion_sduration_sshort
Short state transition.

\var CubicBezier alumbra::design_tokens::Tokens::alumbra_token_motion_seasing_semphasized
Emphasized easing curve.

\var CubicBezier alumbra::design_tokens::Tokens::alumbra_token_motion_seasing_sstandard
Standard easing curve.

\var double alumbra::design_tokens::Tokens::alumbra_token_opacity_sdisabled
Opacity for disabled controls.

\var Pixels alumbra::design_tokens::Tokens::alumbra_token_radius_slarge
Large panel corner radius.

\var Pixels alumbra::design_tokens::Tokens::alumbra_token_radius_smedium
Standard corner radius.

\var Pixels alumbra::design_tokens::Tokens::alumbra_token_radius_spill
Fully rounded control radius.

\var Pixels alumbra::design_tokens::Tokens::alumbra_token_radius_ssmall
Small corner radius.

\var Pixels alumbra::design_tokens::Tokens::alumbra_token_spacing_sextra_ularge
Extra large separation.

\var Pixels alumbra::design_tokens::Tokens::alumbra_token_spacing_slarge
Large separation.

\var Pixels alumbra::design_tokens::Tokens::alumbra_token_spacing_smedium
Standard separation.

\var Pixels alumbra::design_tokens::Tokens::alumbra_token_spacing_snone
No separation.

\var Pixels alumbra::design_tokens::Tokens::alumbra_token_spacing_ssmall
Small separation.

\var Pixels alumbra::design_tokens::Tokens::alumbra_token_spacing_stight
Tight separation.

\var Pixels alumbra::design_tokens::Tokens::alumbra_token_target_sminimum
Minimum interactive target width and height for the active density context.

\var FontFamily alumbra::design_tokens::Tokens::alumbra_token_typography_sfont_ufamily_sui
Platform-neutral system UI font family.

\var Pixels alumbra::design_tokens::Tokens::alumbra_token_typography_sfont_usize_sbody
Default readable body size.

\var Pixels alumbra::design_tokens::Tokens::alumbra_token_typography_sfont_usize_scaption
Caption and metadata size.

\var Pixels alumbra::design_tokens::Tokens::alumbra_token_typography_sfont_usize_sdisplay
Prominent display size.

\var Pixels alumbra::design_tokens::Tokens::alumbra_token_typography_sfont_usize_stitle
Player title size.

\var double alumbra::design_tokens::Tokens::alumbra_token_typography_sfont_uweight_sbold
Strong text weight.

\var double alumbra::design_tokens::Tokens::alumbra_token_typography_sfont_uweight_smedium
Emphasized text weight.

\var double alumbra::design_tokens::Tokens::alumbra_token_typography_sfont_uweight_sregular
Default text weight.

\var double alumbra::design_tokens::Tokens::alumbra_token_typography_sline_uheight_sbody
Body line-height ratio.

\var double alumbra::design_tokens::Tokens::alumbra_token_typography_sline_uheight_scompact
Compact line-height ratio.

\var Color alumbra::design_tokens::Tokens::alumbra_token_player_sad_sband_sbackground
Distinct reserved ad band path used to prove injective output-name mapping.

\var Color alumbra::design_tokens::Tokens::alumbra_token_player_sad_uband_sbackground
Reserved ad-band surface; path mapping example preserving underscores.

\var Color alumbra::design_tokens::Tokens::alumbra_token_player_sad_uchrome_sbadge_sbackground
Reserved surface for player-owned ad chrome in M5.

\var Color alumbra::design_tokens::Tokens::alumbra_token_player_sbutton_sbackground
Default player button surface.

\var Color alumbra::design_tokens::Tokens::alumbra_token_player_sbutton_sbackground_sdisabled
Disabled button surface.

\var Color alumbra::design_tokens::Tokens::alumbra_token_player_sbutton_sbackground_sfocus
Focused button fill; the separate player.focus.ring token supplies the focus indicator.

\var Color alumbra::design_tokens::Tokens::alumbra_token_player_sbutton_sbackground_shover
Hovered or D-pad highlighted button surface.

\var Color alumbra::design_tokens::Tokens::alumbra_token_player_sbutton_sbackground_spressed
Pressed button surface.

\var Color alumbra::design_tokens::Tokens::alumbra_token_player_sbutton_sforeground
Default player button label and icon on the default surface.

\var Color alumbra::design_tokens::Tokens::alumbra_token_player_sbutton_sforeground_sdisabled
Disabled player button label and icon; disabled controls are exempt from the enabled-state contrast threshold.

\var Color alumbra::design_tokens::Tokens::alumbra_token_player_sbutton_sforeground_sfocus
Focused player button label and icon on the focused button fill.

\var Color alumbra::design_tokens::Tokens::alumbra_token_player_sbutton_sforeground_shover
Hovered or D-pad highlighted player button label and icon.

\var Color alumbra::design_tokens::Tokens::alumbra_token_player_sbutton_sforeground_spressed
Pressed player button label and icon.

\var Pixels alumbra::design_tokens::Tokens::alumbra_token_player_sbutton_sradius
Player button corner radius.

\var Pixels alumbra::design_tokens::Tokens::alumbra_token_player_scaptions_sfont_usize
Caption text size; reserved for M3 user-adjustable styles.

\var Color alumbra::design_tokens::Tokens::alumbra_token_player_scaptions_stext
Caption text foreground; reserved for the M3 caption-style milestone.

\var Color alumbra::design_tokens::Tokens::alumbra_token_player_scaptions_swindow
Caption background window; reserved for the M3 caption-style milestone.

\var Color alumbra::design_tokens::Tokens::alumbra_token_player_sfocus_sring
Visible keyboard and D-pad focus ring.

\var Color alumbra::design_tokens::Tokens::alumbra_token_player_sseekbar_splayed
Played seekbar track color.

\var Color alumbra::design_tokens::Tokens::alumbra_token_player_sseekbar_strack
Unplayed seekbar track.

\struct alumbra::design_tokens::Overrides
Values that replace resolved tokens; an empty member leaves the token as resolved.

An override wins over the value a token would derive through an alias.

\var std::optional<Color> alumbra::design_tokens::Overrides::alumbra_token_color_saccent_splayed
Replacement for the token color.accent.played.

\var std::optional<Color> alumbra::design_tokens::Overrides::alumbra_token_color_sborder_ssubtle
Replacement for the token color.border.subtle.

\var std::optional<Color> alumbra::design_tokens::Overrides::alumbra_token_color_scontrol_sbackground
Replacement for the token color.control.background.

\var std::optional<Color> alumbra::design_tokens::Overrides::alumbra_token_color_scontrol_sdisabled
Replacement for the token color.control.disabled.

\var std::optional<Color> alumbra::design_tokens::Overrides::alumbra_token_color_scontrol_sforeground
Replacement for the token color.control.foreground.

\var std::optional<Color> alumbra::design_tokens::Overrides::alumbra_token_color_scontrol_shover
Replacement for the token color.control.hover.

\var std::optional<Color> alumbra::design_tokens::Overrides::alumbra_token_color_scontrol_spressed
Replacement for the token color.control.pressed.

\var std::optional<Color> alumbra::design_tokens::Overrides::alumbra_token_color_sfocus_sring
Replacement for the token color.focus.ring.

\var std::optional<Color> alumbra::design_tokens::Overrides::alumbra_token_color_ssurface_sbase
Replacement for the token color.surface.base.

\var std::optional<Color> alumbra::design_tokens::Overrides::alumbra_token_color_ssurface_selevated
Replacement for the token color.surface.elevated.

\var std::optional<Color> alumbra::design_tokens::Overrides::alumbra_token_color_ssurface_soverlay
Replacement for the token color.surface.overlay.

\var std::optional<Color> alumbra::design_tokens::Overrides::alumbra_token_color_ssurface_sscrim
Replacement for the token color.surface.scrim.

\var std::optional<Color> alumbra::design_tokens::Overrides::alumbra_token_color_stext_sdisabled
Replacement for the token color.text.disabled.

\var std::optional<Color> alumbra::design_tokens::Overrides::alumbra_token_color_stext_sprimary
Replacement for the token color.text.primary.

\var std::optional<Color> alumbra::design_tokens::Overrides::alumbra_token_color_stext_ssecondary
Replacement for the token color.text.secondary.

\var std::optional<Shadow> alumbra::design_tokens::Overrides::alumbra_token_elevation_smodal
Replacement for the token elevation.modal.

\var std::optional<Shadow> alumbra::design_tokens::Overrides::alumbra_token_elevation_spanel
Replacement for the token elevation.panel.

\var std::optional<Shadow> alumbra::design_tokens::Overrides::alumbra_token_elevation_sraised
Replacement for the token elevation.raised.

\var std::optional<double> alumbra::design_tokens::Overrides::alumbra_token_layer_sad_uchrome
Replacement for the token layer.ad_chrome.

\var std::optional<double> alumbra::design_tokens::Overrides::alumbra_token_layer_sad_usdk_ucontainer
Replacement for the token layer.ad_sdk_container.

\var std::optional<double> alumbra::design_tokens::Overrides::alumbra_token_layer_scaptions
Replacement for the token layer.captions.

\var std::optional<double> alumbra::design_tokens::Overrides::alumbra_token_layer_scontrols
Replacement for the token layer.controls.

\var std::optional<double> alumbra::design_tokens::Overrides::alumbra_token_layer_svideo_usurface
Replacement for the token layer.video_surface.

\var std::optional<double> alumbra::design_tokens::Overrides::alumbra_token_luminance_shdr_uhighlight_uceiling
Replacement for the token luminance.hdr_highlight_ceiling.

\var std::optional<double> alumbra::design_tokens::Overrides::alumbra_token_luminance_sreference_uwhite
Replacement for the token luminance.reference_white.

\var std::optional<Milliseconds> alumbra::design_tokens::Overrides::alumbra_token_motion_sduration_sinstant
Replacement for the token motion.duration.instant.

\var std::optional<Milliseconds> alumbra::design_tokens::Overrides::alumbra_token_motion_sduration_slong
Replacement for the token motion.duration.long.

\var std::optional<Milliseconds> alumbra::design_tokens::Overrides::alumbra_token_motion_sduration_smedium
Replacement for the token motion.duration.medium.

\var std::optional<Milliseconds> alumbra::design_tokens::Overrides::alumbra_token_motion_sduration_sshort
Replacement for the token motion.duration.short.

\var std::optional<CubicBezier> alumbra::design_tokens::Overrides::alumbra_token_motion_seasing_semphasized
Replacement for the token motion.easing.emphasized.

\var std::optional<CubicBezier> alumbra::design_tokens::Overrides::alumbra_token_motion_seasing_sstandard
Replacement for the token motion.easing.standard.

\var std::optional<double> alumbra::design_tokens::Overrides::alumbra_token_opacity_sdisabled
Replacement for the token opacity.disabled.

\var std::optional<Pixels> alumbra::design_tokens::Overrides::alumbra_token_radius_slarge
Replacement for the token radius.large.

\var std::optional<Pixels> alumbra::design_tokens::Overrides::alumbra_token_radius_smedium
Replacement for the token radius.medium.

\var std::optional<Pixels> alumbra::design_tokens::Overrides::alumbra_token_radius_spill
Replacement for the token radius.pill.

\var std::optional<Pixels> alumbra::design_tokens::Overrides::alumbra_token_radius_ssmall
Replacement for the token radius.small.

\var std::optional<Pixels> alumbra::design_tokens::Overrides::alumbra_token_spacing_sextra_ularge
Replacement for the token spacing.extra_large.

\var std::optional<Pixels> alumbra::design_tokens::Overrides::alumbra_token_spacing_slarge
Replacement for the token spacing.large.

\var std::optional<Pixels> alumbra::design_tokens::Overrides::alumbra_token_spacing_smedium
Replacement for the token spacing.medium.

\var std::optional<Pixels> alumbra::design_tokens::Overrides::alumbra_token_spacing_snone
Replacement for the token spacing.none.

\var std::optional<Pixels> alumbra::design_tokens::Overrides::alumbra_token_spacing_ssmall
Replacement for the token spacing.small.

\var std::optional<Pixels> alumbra::design_tokens::Overrides::alumbra_token_spacing_stight
Replacement for the token spacing.tight.

\var std::optional<Pixels> alumbra::design_tokens::Overrides::alumbra_token_target_sminimum
Replacement for the token target.minimum.

\var std::optional<FontFamily> alumbra::design_tokens::Overrides::alumbra_token_typography_sfont_ufamily_sui
Replacement for the token typography.font_family.ui.

\var std::optional<Pixels> alumbra::design_tokens::Overrides::alumbra_token_typography_sfont_usize_sbody
Replacement for the token typography.font_size.body.

\var std::optional<Pixels> alumbra::design_tokens::Overrides::alumbra_token_typography_sfont_usize_scaption
Replacement for the token typography.font_size.caption.

\var std::optional<Pixels> alumbra::design_tokens::Overrides::alumbra_token_typography_sfont_usize_sdisplay
Replacement for the token typography.font_size.display.

\var std::optional<Pixels> alumbra::design_tokens::Overrides::alumbra_token_typography_sfont_usize_stitle
Replacement for the token typography.font_size.title.

\var std::optional<double> alumbra::design_tokens::Overrides::alumbra_token_typography_sfont_uweight_sbold
Replacement for the token typography.font_weight.bold.

\var std::optional<double> alumbra::design_tokens::Overrides::alumbra_token_typography_sfont_uweight_smedium
Replacement for the token typography.font_weight.medium.

\var std::optional<double> alumbra::design_tokens::Overrides::alumbra_token_typography_sfont_uweight_sregular
Replacement for the token typography.font_weight.regular.

\var std::optional<double> alumbra::design_tokens::Overrides::alumbra_token_typography_sline_uheight_sbody
Replacement for the token typography.line_height.body.

\var std::optional<double> alumbra::design_tokens::Overrides::alumbra_token_typography_sline_uheight_scompact
Replacement for the token typography.line_height.compact.

\var std::optional<Color> alumbra::design_tokens::Overrides::alumbra_token_player_sad_sband_sbackground
Replacement for the token player.ad.band.background.

\var std::optional<Color> alumbra::design_tokens::Overrides::alumbra_token_player_sad_uband_sbackground
Replacement for the token player.ad_band.background.

\var std::optional<Color> alumbra::design_tokens::Overrides::alumbra_token_player_sad_uchrome_sbadge_sbackground
Replacement for the token player.ad_chrome.badge.background.

\var std::optional<Color> alumbra::design_tokens::Overrides::alumbra_token_player_sbutton_sbackground
Replacement for the token player.button.background.

\var std::optional<Color> alumbra::design_tokens::Overrides::alumbra_token_player_sbutton_sbackground_sdisabled
Replacement for the token player.button.background.disabled.

\var std::optional<Color> alumbra::design_tokens::Overrides::alumbra_token_player_sbutton_sbackground_sfocus
Replacement for the token player.button.background.focus.

\var std::optional<Color> alumbra::design_tokens::Overrides::alumbra_token_player_sbutton_sbackground_shover
Replacement for the token player.button.background.hover.

\var std::optional<Color> alumbra::design_tokens::Overrides::alumbra_token_player_sbutton_sbackground_spressed
Replacement for the token player.button.background.pressed.

\var std::optional<Color> alumbra::design_tokens::Overrides::alumbra_token_player_sbutton_sforeground
Replacement for the token player.button.foreground.

\var std::optional<Color> alumbra::design_tokens::Overrides::alumbra_token_player_sbutton_sforeground_sdisabled
Replacement for the token player.button.foreground.disabled.

\var std::optional<Color> alumbra::design_tokens::Overrides::alumbra_token_player_sbutton_sforeground_sfocus
Replacement for the token player.button.foreground.focus.

\var std::optional<Color> alumbra::design_tokens::Overrides::alumbra_token_player_sbutton_sforeground_shover
Replacement for the token player.button.foreground.hover.

\var std::optional<Color> alumbra::design_tokens::Overrides::alumbra_token_player_sbutton_sforeground_spressed
Replacement for the token player.button.foreground.pressed.

\var std::optional<Pixels> alumbra::design_tokens::Overrides::alumbra_token_player_sbutton_sradius
Replacement for the token player.button.radius.

\var std::optional<Pixels> alumbra::design_tokens::Overrides::alumbra_token_player_scaptions_sfont_usize
Replacement for the token player.captions.font_size.

\var std::optional<Color> alumbra::design_tokens::Overrides::alumbra_token_player_scaptions_stext
Replacement for the token player.captions.text.

\var std::optional<Color> alumbra::design_tokens::Overrides::alumbra_token_player_scaptions_swindow
Replacement for the token player.captions.window.

\var std::optional<Color> alumbra::design_tokens::Overrides::alumbra_token_player_sfocus_sring
Replacement for the token player.focus.ring.

\var std::optional<Color> alumbra::design_tokens::Overrides::alumbra_token_player_sseekbar_splayed
Replacement for the token player.seekbar.played.

\var std::optional<Color> alumbra::design_tokens::Overrides::alumbra_token_player_sseekbar_strack
Replacement for the token player.seekbar.track.

\fn Tokens alumbra::design_tokens::Resolve(const Context& context, const Overrides& overrides)
Resolves every public token for a context, then applies the overrides.

Both arguments default to the default context with no overrides. The result is a constant expression.

\fn void alumbra::design_tokens::VisitTokens(const Tokens& tokens, Visitor& visitor)
Calls the visitor once for each public token with its name and resolved value.
