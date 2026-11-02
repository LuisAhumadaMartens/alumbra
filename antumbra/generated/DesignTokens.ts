export type Theme = "dark" | "light";
export type Contrast = "normal" | "high";
export type Density = "phone" | "tablet" | "desktop" | "tv";
export type Motion = "full" | "reduced";
export type DynamicRange = "sdr" | "hdr";

export interface Context {
  readonly theme: Theme;
  readonly contrast: Contrast;
  readonly density: Density;
  readonly motion: Motion;
  readonly dynamicRange: DynamicRange;
}

export const contexts = {
  theme: ["dark", "light"],
  contrast: ["normal", "high"],
  density: ["phone", "tablet", "desktop", "tv"],
  motion: ["full", "reduced"],
  dynamicRange: ["sdr", "hdr"],
} as const;

export const defaultContext: Context = {
  theme: "dark",
  contrast: "normal",
  density: "desktop",
  motion: "full",
  dynamicRange: "sdr",
};

export type ColorSpace = "srgb" | "display-p3" | "rec2020";

export type TokenType =
  | "color"
  | "dimension"
  | "duration"
  | "number"
  | "fontWeight"
  | "fontFamily"
  | "cubicBezier"
  | "shadow";

export interface Rgb {
  readonly red: number;
  readonly green: number;
  readonly blue: number;
}

export interface Color {
  readonly colorSpace: ColorSpace;
  readonly components: Rgb;
  readonly alpha: number;
  readonly srgb: Rgb;
}

export interface CubicBezier {
  readonly x1: number;
  readonly y1: number;
  readonly x2: number;
  readonly y2: number;
}

export interface ShadowLayer {
  readonly color: Color;
  readonly offsetX: number;
  readonly offsetY: number;
  readonly blur: number;
  readonly spread: number;
  readonly inset: boolean;
}

export type FontFamily = readonly string[];

export type Shadow = readonly ShadowLayer[];

export interface Tokens {
  readonly alumbra_token_color_saccent_splayed: Color;
  readonly alumbra_token_color_sborder_ssubtle: Color;
  readonly alumbra_token_color_scontrol_sbackground: Color;
  readonly alumbra_token_color_scontrol_sdisabled: Color;
  readonly alumbra_token_color_scontrol_sforeground: Color;
  readonly alumbra_token_color_scontrol_shover: Color;
  readonly alumbra_token_color_scontrol_spressed: Color;
  readonly alumbra_token_color_sfocus_sring: Color;
  readonly alumbra_token_color_ssurface_sbase: Color;
  readonly alumbra_token_color_ssurface_selevated: Color;
  readonly alumbra_token_color_ssurface_soverlay: Color;
  readonly alumbra_token_color_ssurface_sscrim: Color;
  readonly alumbra_token_color_stext_sdisabled: Color;
  readonly alumbra_token_color_stext_sprimary: Color;
  readonly alumbra_token_color_stext_ssecondary: Color;
  readonly alumbra_token_elevation_smodal: Shadow;
  readonly alumbra_token_elevation_spanel: Shadow;
  readonly alumbra_token_elevation_sraised: Shadow;
  readonly alumbra_token_layer_sad_uchrome: number;
  readonly alumbra_token_layer_sad_usdk_ucontainer: number;
  readonly alumbra_token_layer_scaptions: number;
  readonly alumbra_token_layer_scontrols: number;
  readonly alumbra_token_layer_svideo_usurface: number;
  readonly alumbra_token_luminance_shdr_uhighlight_uceiling: number;
  readonly alumbra_token_luminance_sreference_uwhite: number;
  readonly alumbra_token_motion_sduration_sinstant: number;
  readonly alumbra_token_motion_sduration_slong: number;
  readonly alumbra_token_motion_sduration_smedium: number;
  readonly alumbra_token_motion_sduration_sshort: number;
  readonly alumbra_token_motion_seasing_semphasized: CubicBezier;
  readonly alumbra_token_motion_seasing_sstandard: CubicBezier;
  readonly alumbra_token_opacity_sdisabled: number;
  readonly alumbra_token_radius_slarge: number;
  readonly alumbra_token_radius_smedium: number;
  readonly alumbra_token_radius_spill: number;
  readonly alumbra_token_radius_ssmall: number;
  readonly alumbra_token_spacing_sextra_ularge: number;
  readonly alumbra_token_spacing_slarge: number;
  readonly alumbra_token_spacing_smedium: number;
  readonly alumbra_token_spacing_snone: number;
  readonly alumbra_token_spacing_ssmall: number;
  readonly alumbra_token_spacing_stight: number;
  readonly alumbra_token_target_sminimum: number;
  readonly alumbra_token_typography_sfont_ufamily_sui: FontFamily;
  readonly alumbra_token_typography_sfont_usize_sbody: number;
  readonly alumbra_token_typography_sfont_usize_scaption: number;
  readonly alumbra_token_typography_sfont_usize_sdisplay: number;
  readonly alumbra_token_typography_sfont_usize_stitle: number;
  readonly alumbra_token_typography_sfont_uweight_sbold: number;
  readonly alumbra_token_typography_sfont_uweight_smedium: number;
  readonly alumbra_token_typography_sfont_uweight_sregular: number;
  readonly alumbra_token_typography_sline_uheight_sbody: number;
  readonly alumbra_token_typography_sline_uheight_scompact: number;
  readonly alumbra_token_player_sad_sband_sbackground: Color;
  readonly alumbra_token_player_sad_uband_sbackground: Color;
  readonly alumbra_token_player_sad_uchrome_sbadge_sbackground: Color;
  readonly alumbra_token_player_sbutton_sbackground: Color;
  readonly alumbra_token_player_sbutton_sbackground_sdisabled: Color;
  readonly alumbra_token_player_sbutton_sbackground_sfocus: Color;
  readonly alumbra_token_player_sbutton_sbackground_shover: Color;
  readonly alumbra_token_player_sbutton_sbackground_spressed: Color;
  readonly alumbra_token_player_sbutton_sforeground: Color;
  readonly alumbra_token_player_sbutton_sforeground_sdisabled: Color;
  readonly alumbra_token_player_sbutton_sforeground_sfocus: Color;
  readonly alumbra_token_player_sbutton_sforeground_shover: Color;
  readonly alumbra_token_player_sbutton_sforeground_spressed: Color;
  readonly alumbra_token_player_sbutton_sradius: number;
  readonly alumbra_token_player_scaptions_sfont_usize: number;
  readonly alumbra_token_player_scaptions_stext: Color;
  readonly alumbra_token_player_scaptions_swindow: Color;
  readonly alumbra_token_player_sfocus_sring: Color;
  readonly alumbra_token_player_sseekbar_splayed: Color;
  readonly alumbra_token_player_sseekbar_strack: Color;
}

export type Overrides = Partial<Tokens>;

export const tokenNames: { readonly [K in keyof Tokens]: string } = {
  alumbra_token_color_saccent_splayed: "color.accent.played",
  alumbra_token_color_sborder_ssubtle: "color.border.subtle",
  alumbra_token_color_scontrol_sbackground: "color.control.background",
  alumbra_token_color_scontrol_sdisabled: "color.control.disabled",
  alumbra_token_color_scontrol_sforeground: "color.control.foreground",
  alumbra_token_color_scontrol_shover: "color.control.hover",
  alumbra_token_color_scontrol_spressed: "color.control.pressed",
  alumbra_token_color_sfocus_sring: "color.focus.ring",
  alumbra_token_color_ssurface_sbase: "color.surface.base",
  alumbra_token_color_ssurface_selevated: "color.surface.elevated",
  alumbra_token_color_ssurface_soverlay: "color.surface.overlay",
  alumbra_token_color_ssurface_sscrim: "color.surface.scrim",
  alumbra_token_color_stext_sdisabled: "color.text.disabled",
  alumbra_token_color_stext_sprimary: "color.text.primary",
  alumbra_token_color_stext_ssecondary: "color.text.secondary",
  alumbra_token_elevation_smodal: "elevation.modal",
  alumbra_token_elevation_spanel: "elevation.panel",
  alumbra_token_elevation_sraised: "elevation.raised",
  alumbra_token_layer_sad_uchrome: "layer.ad_chrome",
  alumbra_token_layer_sad_usdk_ucontainer: "layer.ad_sdk_container",
  alumbra_token_layer_scaptions: "layer.captions",
  alumbra_token_layer_scontrols: "layer.controls",
  alumbra_token_layer_svideo_usurface: "layer.video_surface",
  alumbra_token_luminance_shdr_uhighlight_uceiling: "luminance.hdr_highlight_ceiling",
  alumbra_token_luminance_sreference_uwhite: "luminance.reference_white",
  alumbra_token_motion_sduration_sinstant: "motion.duration.instant",
  alumbra_token_motion_sduration_slong: "motion.duration.long",
  alumbra_token_motion_sduration_smedium: "motion.duration.medium",
  alumbra_token_motion_sduration_sshort: "motion.duration.short",
  alumbra_token_motion_seasing_semphasized: "motion.easing.emphasized",
  alumbra_token_motion_seasing_sstandard: "motion.easing.standard",
  alumbra_token_opacity_sdisabled: "opacity.disabled",
  alumbra_token_radius_slarge: "radius.large",
  alumbra_token_radius_smedium: "radius.medium",
  alumbra_token_radius_spill: "radius.pill",
  alumbra_token_radius_ssmall: "radius.small",
  alumbra_token_spacing_sextra_ularge: "spacing.extra_large",
  alumbra_token_spacing_slarge: "spacing.large",
  alumbra_token_spacing_smedium: "spacing.medium",
  alumbra_token_spacing_snone: "spacing.none",
  alumbra_token_spacing_ssmall: "spacing.small",
  alumbra_token_spacing_stight: "spacing.tight",
  alumbra_token_target_sminimum: "target.minimum",
  alumbra_token_typography_sfont_ufamily_sui: "typography.font_family.ui",
  alumbra_token_typography_sfont_usize_sbody: "typography.font_size.body",
  alumbra_token_typography_sfont_usize_scaption: "typography.font_size.caption",
  alumbra_token_typography_sfont_usize_sdisplay: "typography.font_size.display",
  alumbra_token_typography_sfont_usize_stitle: "typography.font_size.title",
  alumbra_token_typography_sfont_uweight_sbold: "typography.font_weight.bold",
  alumbra_token_typography_sfont_uweight_smedium: "typography.font_weight.medium",
  alumbra_token_typography_sfont_uweight_sregular: "typography.font_weight.regular",
  alumbra_token_typography_sline_uheight_sbody: "typography.line_height.body",
  alumbra_token_typography_sline_uheight_scompact: "typography.line_height.compact",
  alumbra_token_player_sad_sband_sbackground: "player.ad.band.background",
  alumbra_token_player_sad_uband_sbackground: "player.ad_band.background",
  alumbra_token_player_sad_uchrome_sbadge_sbackground: "player.ad_chrome.badge.background",
  alumbra_token_player_sbutton_sbackground: "player.button.background",
  alumbra_token_player_sbutton_sbackground_sdisabled: "player.button.background.disabled",
  alumbra_token_player_sbutton_sbackground_sfocus: "player.button.background.focus",
  alumbra_token_player_sbutton_sbackground_shover: "player.button.background.hover",
  alumbra_token_player_sbutton_sbackground_spressed: "player.button.background.pressed",
  alumbra_token_player_sbutton_sforeground: "player.button.foreground",
  alumbra_token_player_sbutton_sforeground_sdisabled: "player.button.foreground.disabled",
  alumbra_token_player_sbutton_sforeground_sfocus: "player.button.foreground.focus",
  alumbra_token_player_sbutton_sforeground_shover: "player.button.foreground.hover",
  alumbra_token_player_sbutton_sforeground_spressed: "player.button.foreground.pressed",
  alumbra_token_player_sbutton_sradius: "player.button.radius",
  alumbra_token_player_scaptions_sfont_usize: "player.captions.font_size",
  alumbra_token_player_scaptions_stext: "player.captions.text",
  alumbra_token_player_scaptions_swindow: "player.captions.window",
  alumbra_token_player_sfocus_sring: "player.focus.ring",
  alumbra_token_player_sseekbar_splayed: "player.seekbar.played",
  alumbra_token_player_sseekbar_strack: "player.seekbar.track",
};

export const cssProperties: { readonly [K in keyof Tokens]: string } = {
  alumbra_token_color_saccent_splayed: "--alumbra-color-accent-played",
  alumbra_token_color_sborder_ssubtle: "--alumbra-color-border-subtle",
  alumbra_token_color_scontrol_sbackground: "--alumbra-color-control-background",
  alumbra_token_color_scontrol_sdisabled: "--alumbra-color-control-disabled",
  alumbra_token_color_scontrol_sforeground: "--alumbra-color-control-foreground",
  alumbra_token_color_scontrol_shover: "--alumbra-color-control-hover",
  alumbra_token_color_scontrol_spressed: "--alumbra-color-control-pressed",
  alumbra_token_color_sfocus_sring: "--alumbra-color-focus-ring",
  alumbra_token_color_ssurface_sbase: "--alumbra-color-surface-base",
  alumbra_token_color_ssurface_selevated: "--alumbra-color-surface-elevated",
  alumbra_token_color_ssurface_soverlay: "--alumbra-color-surface-overlay",
  alumbra_token_color_ssurface_sscrim: "--alumbra-color-surface-scrim",
  alumbra_token_color_stext_sdisabled: "--alumbra-color-text-disabled",
  alumbra_token_color_stext_sprimary: "--alumbra-color-text-primary",
  alumbra_token_color_stext_ssecondary: "--alumbra-color-text-secondary",
  alumbra_token_elevation_smodal: "--alumbra-elevation-modal",
  alumbra_token_elevation_spanel: "--alumbra-elevation-panel",
  alumbra_token_elevation_sraised: "--alumbra-elevation-raised",
  alumbra_token_layer_sad_uchrome: "--alumbra-layer-ad_chrome",
  alumbra_token_layer_sad_usdk_ucontainer: "--alumbra-layer-ad_sdk_container",
  alumbra_token_layer_scaptions: "--alumbra-layer-captions",
  alumbra_token_layer_scontrols: "--alumbra-layer-controls",
  alumbra_token_layer_svideo_usurface: "--alumbra-layer-video_surface",
  alumbra_token_luminance_shdr_uhighlight_uceiling: "--alumbra-luminance-hdr_highlight_ceiling",
  alumbra_token_luminance_sreference_uwhite: "--alumbra-luminance-reference_white",
  alumbra_token_motion_sduration_sinstant: "--alumbra-motion-duration-instant",
  alumbra_token_motion_sduration_slong: "--alumbra-motion-duration-long",
  alumbra_token_motion_sduration_smedium: "--alumbra-motion-duration-medium",
  alumbra_token_motion_sduration_sshort: "--alumbra-motion-duration-short",
  alumbra_token_motion_seasing_semphasized: "--alumbra-motion-easing-emphasized",
  alumbra_token_motion_seasing_sstandard: "--alumbra-motion-easing-standard",
  alumbra_token_opacity_sdisabled: "--alumbra-opacity-disabled",
  alumbra_token_radius_slarge: "--alumbra-radius-large",
  alumbra_token_radius_smedium: "--alumbra-radius-medium",
  alumbra_token_radius_spill: "--alumbra-radius-pill",
  alumbra_token_radius_ssmall: "--alumbra-radius-small",
  alumbra_token_spacing_sextra_ularge: "--alumbra-spacing-extra_large",
  alumbra_token_spacing_slarge: "--alumbra-spacing-large",
  alumbra_token_spacing_smedium: "--alumbra-spacing-medium",
  alumbra_token_spacing_snone: "--alumbra-spacing-none",
  alumbra_token_spacing_ssmall: "--alumbra-spacing-small",
  alumbra_token_spacing_stight: "--alumbra-spacing-tight",
  alumbra_token_target_sminimum: "--alumbra-target-minimum",
  alumbra_token_typography_sfont_ufamily_sui: "--alumbra-typography-font_family-ui",
  alumbra_token_typography_sfont_usize_sbody: "--alumbra-typography-font_size-body",
  alumbra_token_typography_sfont_usize_scaption: "--alumbra-typography-font_size-caption",
  alumbra_token_typography_sfont_usize_sdisplay: "--alumbra-typography-font_size-display",
  alumbra_token_typography_sfont_usize_stitle: "--alumbra-typography-font_size-title",
  alumbra_token_typography_sfont_uweight_sbold: "--alumbra-typography-font_weight-bold",
  alumbra_token_typography_sfont_uweight_smedium: "--alumbra-typography-font_weight-medium",
  alumbra_token_typography_sfont_uweight_sregular: "--alumbra-typography-font_weight-regular",
  alumbra_token_typography_sline_uheight_sbody: "--alumbra-typography-line_height-body",
  alumbra_token_typography_sline_uheight_scompact: "--alumbra-typography-line_height-compact",
  alumbra_token_player_sad_sband_sbackground: "--alumbra-player-ad-band-background",
  alumbra_token_player_sad_uband_sbackground: "--alumbra-player-ad_band-background",
  alumbra_token_player_sad_uchrome_sbadge_sbackground: "--alumbra-player-ad_chrome-badge-background",
  alumbra_token_player_sbutton_sbackground: "--alumbra-player-button-background",
  alumbra_token_player_sbutton_sbackground_sdisabled: "--alumbra-player-button-background-disabled",
  alumbra_token_player_sbutton_sbackground_sfocus: "--alumbra-player-button-background-focus",
  alumbra_token_player_sbutton_sbackground_shover: "--alumbra-player-button-background-hover",
  alumbra_token_player_sbutton_sbackground_spressed: "--alumbra-player-button-background-pressed",
  alumbra_token_player_sbutton_sforeground: "--alumbra-player-button-foreground",
  alumbra_token_player_sbutton_sforeground_sdisabled: "--alumbra-player-button-foreground-disabled",
  alumbra_token_player_sbutton_sforeground_sfocus: "--alumbra-player-button-foreground-focus",
  alumbra_token_player_sbutton_sforeground_shover: "--alumbra-player-button-foreground-hover",
  alumbra_token_player_sbutton_sforeground_spressed: "--alumbra-player-button-foreground-pressed",
  alumbra_token_player_sbutton_sradius: "--alumbra-player-button-radius",
  alumbra_token_player_scaptions_sfont_usize: "--alumbra-player-captions-font_size",
  alumbra_token_player_scaptions_stext: "--alumbra-player-captions-text",
  alumbra_token_player_scaptions_swindow: "--alumbra-player-captions-window",
  alumbra_token_player_sfocus_sring: "--alumbra-player-focus-ring",
  alumbra_token_player_sseekbar_splayed: "--alumbra-player-seekbar-played",
  alumbra_token_player_sseekbar_strack: "--alumbra-player-seekbar-track",
};

export const tokenTypes: { readonly [K in keyof Tokens]: TokenType } = {
  alumbra_token_color_saccent_splayed: "color",
  alumbra_token_color_sborder_ssubtle: "color",
  alumbra_token_color_scontrol_sbackground: "color",
  alumbra_token_color_scontrol_sdisabled: "color",
  alumbra_token_color_scontrol_sforeground: "color",
  alumbra_token_color_scontrol_shover: "color",
  alumbra_token_color_scontrol_spressed: "color",
  alumbra_token_color_sfocus_sring: "color",
  alumbra_token_color_ssurface_sbase: "color",
  alumbra_token_color_ssurface_selevated: "color",
  alumbra_token_color_ssurface_soverlay: "color",
  alumbra_token_color_ssurface_sscrim: "color",
  alumbra_token_color_stext_sdisabled: "color",
  alumbra_token_color_stext_sprimary: "color",
  alumbra_token_color_stext_ssecondary: "color",
  alumbra_token_elevation_smodal: "shadow",
  alumbra_token_elevation_spanel: "shadow",
  alumbra_token_elevation_sraised: "shadow",
  alumbra_token_layer_sad_uchrome: "number",
  alumbra_token_layer_sad_usdk_ucontainer: "number",
  alumbra_token_layer_scaptions: "number",
  alumbra_token_layer_scontrols: "number",
  alumbra_token_layer_svideo_usurface: "number",
  alumbra_token_luminance_shdr_uhighlight_uceiling: "number",
  alumbra_token_luminance_sreference_uwhite: "number",
  alumbra_token_motion_sduration_sinstant: "duration",
  alumbra_token_motion_sduration_slong: "duration",
  alumbra_token_motion_sduration_smedium: "duration",
  alumbra_token_motion_sduration_sshort: "duration",
  alumbra_token_motion_seasing_semphasized: "cubicBezier",
  alumbra_token_motion_seasing_sstandard: "cubicBezier",
  alumbra_token_opacity_sdisabled: "number",
  alumbra_token_radius_slarge: "dimension",
  alumbra_token_radius_smedium: "dimension",
  alumbra_token_radius_spill: "dimension",
  alumbra_token_radius_ssmall: "dimension",
  alumbra_token_spacing_sextra_ularge: "dimension",
  alumbra_token_spacing_slarge: "dimension",
  alumbra_token_spacing_smedium: "dimension",
  alumbra_token_spacing_snone: "dimension",
  alumbra_token_spacing_ssmall: "dimension",
  alumbra_token_spacing_stight: "dimension",
  alumbra_token_target_sminimum: "dimension",
  alumbra_token_typography_sfont_ufamily_sui: "fontFamily",
  alumbra_token_typography_sfont_usize_sbody: "dimension",
  alumbra_token_typography_sfont_usize_scaption: "dimension",
  alumbra_token_typography_sfont_usize_sdisplay: "dimension",
  alumbra_token_typography_sfont_usize_stitle: "dimension",
  alumbra_token_typography_sfont_uweight_sbold: "fontWeight",
  alumbra_token_typography_sfont_uweight_smedium: "fontWeight",
  alumbra_token_typography_sfont_uweight_sregular: "fontWeight",
  alumbra_token_typography_sline_uheight_sbody: "number",
  alumbra_token_typography_sline_uheight_scompact: "number",
  alumbra_token_player_sad_sband_sbackground: "color",
  alumbra_token_player_sad_uband_sbackground: "color",
  alumbra_token_player_sad_uchrome_sbadge_sbackground: "color",
  alumbra_token_player_sbutton_sbackground: "color",
  alumbra_token_player_sbutton_sbackground_sdisabled: "color",
  alumbra_token_player_sbutton_sbackground_sfocus: "color",
  alumbra_token_player_sbutton_sbackground_shover: "color",
  alumbra_token_player_sbutton_sbackground_spressed: "color",
  alumbra_token_player_sbutton_sforeground: "color",
  alumbra_token_player_sbutton_sforeground_sdisabled: "color",
  alumbra_token_player_sbutton_sforeground_sfocus: "color",
  alumbra_token_player_sbutton_sforeground_shover: "color",
  alumbra_token_player_sbutton_sforeground_spressed: "color",
  alumbra_token_player_sbutton_sradius: "dimension",
  alumbra_token_player_scaptions_sfont_usize: "dimension",
  alumbra_token_player_scaptions_stext: "color",
  alumbra_token_player_scaptions_swindow: "color",
  alumbra_token_player_sfocus_sring: "color",
  alumbra_token_player_sseekbar_splayed: "color",
  alumbra_token_player_sseekbar_strack: "color",
};

export const tokenAliases: { readonly [K in keyof Tokens]?: keyof Tokens } = {
  alumbra_token_player_sad_sband_sbackground: "alumbra_token_color_ssurface_selevated",
  alumbra_token_player_sad_uband_sbackground: "alumbra_token_color_ssurface_soverlay",
  alumbra_token_player_sad_uchrome_sbadge_sbackground: "alumbra_token_color_scontrol_sbackground",
  alumbra_token_player_sbutton_sbackground: "alumbra_token_color_scontrol_sbackground",
  alumbra_token_player_sbutton_sbackground_sdisabled: "alumbra_token_color_scontrol_sdisabled",
  alumbra_token_player_sbutton_sbackground_sfocus: "alumbra_token_color_scontrol_sbackground",
  alumbra_token_player_sbutton_sbackground_shover: "alumbra_token_color_scontrol_shover",
  alumbra_token_player_sbutton_sbackground_spressed: "alumbra_token_color_scontrol_spressed",
  alumbra_token_player_sbutton_sforeground: "alumbra_token_color_scontrol_sforeground",
  alumbra_token_player_sbutton_sforeground_sdisabled: "alumbra_token_color_stext_sdisabled",
  alumbra_token_player_sbutton_sforeground_sfocus: "alumbra_token_color_scontrol_sforeground",
  alumbra_token_player_sbutton_sforeground_shover: "alumbra_token_color_scontrol_sforeground",
  alumbra_token_player_sbutton_sforeground_spressed: "alumbra_token_color_scontrol_sforeground",
  alumbra_token_player_sbutton_sradius: "alumbra_token_radius_smedium",
  alumbra_token_player_scaptions_sfont_usize: "alumbra_token_typography_sfont_usize_scaption",
  alumbra_token_player_scaptions_stext: "alumbra_token_color_stext_sprimary",
  alumbra_token_player_scaptions_swindow: "alumbra_token_color_ssurface_sscrim",
  alumbra_token_player_sfocus_sring: "alumbra_token_color_sfocus_sring",
  alumbra_token_player_sseekbar_splayed: "alumbra_token_color_saccent_splayed",
  alumbra_token_player_sseekbar_strack: "alumbra_token_color_sborder_ssubtle",
};

function rgb(red: number, green: number, blue: number): Rgb {
  return { red, green, blue };
}

function srgb(red: number, green: number, blue: number, alpha = 1): Color {
  const components = rgb(red, green, blue);
  return { colorSpace: "srgb", components, alpha, srgb: components };
}

function bezier(x1: number, y1: number, x2: number, y2: number): CubicBezier {
  return { x1, y1, x2, y2 };
}

function layer(
  color: Color,
  offsetX: number,
  offsetY: number,
  blur: number,
  spread: number,
  inset: boolean,
): ShadowLayer {
  return { color, offsetX, offsetY, blur, spread, inset };
}

function select<T>(
  values: readonly T[],
  context: Context,
  modifiers: readonly (keyof Context)[],
): T {
  let index = 0;
  for (const modifier of modifiers) {
    const options: readonly string[] = contexts[modifier];
    const position = options.indexOf(context[modifier]);
    if (position < 0) {
      throw new RangeError(`Unknown ${modifier} context: ${context[modifier]}`);
    }
    index = index * options.length + position;
  }
  const value = values[index];
  if (value === undefined) {
    throw new RangeError("Token table does not cover its contexts");
  }
  return value;
}

const defaults = {
  alumbra_token_color_saccent_splayed: [
    srgb(0.055, 0.647, 0.914),
  ],
  alumbra_token_color_sborder_ssubtle: [
    srgb(0.392, 0.455, 0.545),
    srgb(1.0, 1.0, 1.0),
    srgb(0.278, 0.333, 0.412),
    srgb(0.0, 0.0, 0.0),
  ],
  alumbra_token_color_scontrol_sbackground: [
    srgb(0.118, 0.161, 0.231),
    srgb(0.0, 0.0, 0.0),
    srgb(0.886, 0.91, 0.941),
    srgb(1.0, 1.0, 1.0),
  ],
  alumbra_token_color_scontrol_sdisabled: [
    srgb(0.118, 0.161, 0.231),
    srgb(0.0, 0.0, 0.0),
    srgb(0.945, 0.961, 0.976),
    srgb(1.0, 1.0, 1.0),
  ],
  alumbra_token_color_scontrol_sforeground: [
    srgb(1.0, 1.0, 1.0),
    srgb(1.0, 1.0, 1.0),
    srgb(0.059, 0.09, 0.165),
    srgb(0.0, 0.0, 0.0),
  ],
  alumbra_token_color_scontrol_shover: [
    srgb(0.2, 0.255, 0.333),
    srgb(0.059, 0.09, 0.165),
    srgb(0.796, 0.835, 0.882),
    srgb(0.886, 0.91, 0.941),
  ],
  alumbra_token_color_scontrol_spressed: [
    srgb(0.278, 0.333, 0.412),
    srgb(0.118, 0.161, 0.231),
    srgb(0.58, 0.639, 0.722),
    srgb(0.796, 0.835, 0.882),
  ],
  alumbra_token_color_sfocus_sring: [
    srgb(0.055, 0.647, 0.914),
    srgb(1.0, 1.0, 0.0),
    srgb(0.012, 0.412, 0.631),
    srgb(0.0, 0.0, 0.0),
  ],
  alumbra_token_color_ssurface_sbase: [
    srgb(0.035, 0.055, 0.094),
    srgb(0.0, 0.0, 0.0),
    srgb(1.0, 1.0, 1.0),
    srgb(1.0, 1.0, 1.0),
  ],
  alumbra_token_color_ssurface_selevated: [
    srgb(0.059, 0.09, 0.165),
    srgb(0.0, 0.0, 0.0),
    srgb(0.973, 0.98, 0.988),
    srgb(1.0, 1.0, 1.0),
  ],
  alumbra_token_color_ssurface_soverlay: [
    srgb(0.118, 0.161, 0.231),
    srgb(0.0, 0.0, 0.0),
    srgb(0.945, 0.961, 0.976),
    srgb(1.0, 1.0, 1.0),
  ],
  alumbra_token_color_ssurface_sscrim: [
    srgb(0.0, 0.0, 0.0, 0.72),
  ],
  alumbra_token_color_stext_sdisabled: [
    srgb(0.392, 0.455, 0.545),
  ],
  alumbra_token_color_stext_sprimary: [
    srgb(0.973, 0.98, 0.988),
    srgb(1.0, 1.0, 1.0),
    srgb(0.059, 0.09, 0.165),
    srgb(0.0, 0.0, 0.0),
  ],
  alumbra_token_color_stext_ssecondary: [
    srgb(0.796, 0.835, 0.882),
    srgb(1.0, 1.0, 1.0),
    srgb(0.2, 0.255, 0.333),
    srgb(0.0, 0.0, 0.0),
  ],
  alumbra_token_elevation_smodal: [
    [layer(srgb(0.0, 0.0, 0.0), 0.0, 8.0, 24.0, 0.0, false)],
  ],
  alumbra_token_elevation_spanel: [
    [layer(srgb(0.0, 0.0, 0.0), 0.0, 4.0, 12.0, 0.0, false)],
  ],
  alumbra_token_elevation_sraised: [
    [layer(srgb(0.0, 0.0, 0.0), 0.0, 2.0, 6.0, 0.0, false)],
  ],
  alumbra_token_layer_sad_uchrome: [
    30.0,
  ],
  alumbra_token_layer_sad_usdk_ucontainer: [
    40.0,
  ],
  alumbra_token_layer_scaptions: [
    10.0,
  ],
  alumbra_token_layer_scontrols: [
    20.0,
  ],
  alumbra_token_layer_svideo_usurface: [
    0.0,
  ],
  alumbra_token_luminance_shdr_uhighlight_uceiling: [
    203.0,
    600.0,
  ],
  alumbra_token_luminance_sreference_uwhite: [
    203.0,
  ],
  alumbra_token_motion_sduration_sinstant: [
    0.0,
  ],
  alumbra_token_motion_sduration_slong: [
    320.0,
    0.0,
  ],
  alumbra_token_motion_sduration_smedium: [
    200.0,
    0.0,
  ],
  alumbra_token_motion_sduration_sshort: [
    120.0,
    0.0,
  ],
  alumbra_token_motion_seasing_semphasized: [
    bezier(0.2, 0.0, 0.0, 1.4),
  ],
  alumbra_token_motion_seasing_sstandard: [
    bezier(0.2, 0.0, 0.0, 1.0),
  ],
  alumbra_token_opacity_sdisabled: [
    0.48,
  ],
  alumbra_token_radius_slarge: [
    16.0,
  ],
  alumbra_token_radius_smedium: [
    8.0,
  ],
  alumbra_token_radius_spill: [
    999.0,
  ],
  alumbra_token_radius_ssmall: [
    4.0,
  ],
  alumbra_token_spacing_sextra_ularge: [
    28.0,
    36.0,
    32.0,
    56.0,
  ],
  alumbra_token_spacing_slarge: [
    20.0,
    28.0,
    24.0,
    40.0,
  ],
  alumbra_token_spacing_smedium: [
    12.0,
    20.0,
    16.0,
    28.0,
  ],
  alumbra_token_spacing_snone: [
    0.0,
  ],
  alumbra_token_spacing_ssmall: [
    8.0,
    8.0,
    8.0,
    16.0,
  ],
  alumbra_token_spacing_stight: [
    4.0,
    4.0,
    4.0,
    8.0,
  ],
  alumbra_token_target_sminimum: [
    48.0,
    48.0,
    44.0,
    72.0,
  ],
  alumbra_token_typography_sfont_ufamily_sui: [
    ["system-ui"],
  ],
  alumbra_token_typography_sfont_usize_sbody: [
    17.0,
    18.0,
    16.0,
    24.0,
  ],
  alumbra_token_typography_sfont_usize_scaption: [
    13.0,
    14.0,
    12.0,
    18.0,
  ],
  alumbra_token_typography_sfont_usize_sdisplay: [
    34.0,
    36.0,
    32.0,
    52.0,
  ],
  alumbra_token_typography_sfont_usize_stitle: [
    25.0,
    26.0,
    24.0,
    36.0,
  ],
  alumbra_token_typography_sfont_uweight_sbold: [
    700.0,
  ],
  alumbra_token_typography_sfont_uweight_smedium: [
    500.0,
  ],
  alumbra_token_typography_sfont_uweight_sregular: [
    400.0,
  ],
  alumbra_token_typography_sline_uheight_sbody: [
    1.5,
  ],
  alumbra_token_typography_sline_uheight_scompact: [
    1.2,
  ],
} as const;

export function resolveTokens(
  context: Context = defaultContext,
  overrides: Overrides = {},
): Tokens {
  const alumbra_token_color_saccent_splayed =
    overrides.alumbra_token_color_saccent_splayed ??
    select(defaults.alumbra_token_color_saccent_splayed, context, []);
  const alumbra_token_color_sborder_ssubtle =
    overrides.alumbra_token_color_sborder_ssubtle ??
    select(defaults.alumbra_token_color_sborder_ssubtle, context, ["theme", "contrast"]);
  const alumbra_token_color_scontrol_sbackground =
    overrides.alumbra_token_color_scontrol_sbackground ??
    select(defaults.alumbra_token_color_scontrol_sbackground, context, ["theme", "contrast"]);
  const alumbra_token_color_scontrol_sdisabled =
    overrides.alumbra_token_color_scontrol_sdisabled ??
    select(defaults.alumbra_token_color_scontrol_sdisabled, context, ["theme", "contrast"]);
  const alumbra_token_color_scontrol_sforeground =
    overrides.alumbra_token_color_scontrol_sforeground ??
    select(defaults.alumbra_token_color_scontrol_sforeground, context, ["theme", "contrast"]);
  const alumbra_token_color_scontrol_shover =
    overrides.alumbra_token_color_scontrol_shover ??
    select(defaults.alumbra_token_color_scontrol_shover, context, ["theme", "contrast"]);
  const alumbra_token_color_scontrol_spressed =
    overrides.alumbra_token_color_scontrol_spressed ??
    select(defaults.alumbra_token_color_scontrol_spressed, context, ["theme", "contrast"]);
  const alumbra_token_color_sfocus_sring =
    overrides.alumbra_token_color_sfocus_sring ??
    select(defaults.alumbra_token_color_sfocus_sring, context, ["theme", "contrast"]);
  const alumbra_token_color_ssurface_sbase =
    overrides.alumbra_token_color_ssurface_sbase ??
    select(defaults.alumbra_token_color_ssurface_sbase, context, ["theme", "contrast"]);
  const alumbra_token_color_ssurface_selevated =
    overrides.alumbra_token_color_ssurface_selevated ??
    select(defaults.alumbra_token_color_ssurface_selevated, context, ["theme", "contrast"]);
  const alumbra_token_color_ssurface_soverlay =
    overrides.alumbra_token_color_ssurface_soverlay ??
    select(defaults.alumbra_token_color_ssurface_soverlay, context, ["theme", "contrast"]);
  const alumbra_token_color_ssurface_sscrim =
    overrides.alumbra_token_color_ssurface_sscrim ??
    select(defaults.alumbra_token_color_ssurface_sscrim, context, []);
  const alumbra_token_color_stext_sdisabled =
    overrides.alumbra_token_color_stext_sdisabled ??
    select(defaults.alumbra_token_color_stext_sdisabled, context, []);
  const alumbra_token_color_stext_sprimary =
    overrides.alumbra_token_color_stext_sprimary ??
    select(defaults.alumbra_token_color_stext_sprimary, context, ["theme", "contrast"]);
  const alumbra_token_color_stext_ssecondary =
    overrides.alumbra_token_color_stext_ssecondary ??
    select(defaults.alumbra_token_color_stext_ssecondary, context, ["theme", "contrast"]);
  const alumbra_token_elevation_smodal =
    overrides.alumbra_token_elevation_smodal ??
    select(defaults.alumbra_token_elevation_smodal, context, []);
  const alumbra_token_elevation_spanel =
    overrides.alumbra_token_elevation_spanel ??
    select(defaults.alumbra_token_elevation_spanel, context, []);
  const alumbra_token_elevation_sraised =
    overrides.alumbra_token_elevation_sraised ??
    select(defaults.alumbra_token_elevation_sraised, context, []);
  const alumbra_token_layer_sad_uchrome =
    overrides.alumbra_token_layer_sad_uchrome ??
    select(defaults.alumbra_token_layer_sad_uchrome, context, []);
  const alumbra_token_layer_sad_usdk_ucontainer =
    overrides.alumbra_token_layer_sad_usdk_ucontainer ??
    select(defaults.alumbra_token_layer_sad_usdk_ucontainer, context, []);
  const alumbra_token_layer_scaptions =
    overrides.alumbra_token_layer_scaptions ??
    select(defaults.alumbra_token_layer_scaptions, context, []);
  const alumbra_token_layer_scontrols =
    overrides.alumbra_token_layer_scontrols ??
    select(defaults.alumbra_token_layer_scontrols, context, []);
  const alumbra_token_layer_svideo_usurface =
    overrides.alumbra_token_layer_svideo_usurface ??
    select(defaults.alumbra_token_layer_svideo_usurface, context, []);
  const alumbra_token_luminance_shdr_uhighlight_uceiling =
    overrides.alumbra_token_luminance_shdr_uhighlight_uceiling ??
    select(defaults.alumbra_token_luminance_shdr_uhighlight_uceiling, context, ["dynamicRange"]);
  const alumbra_token_luminance_sreference_uwhite =
    overrides.alumbra_token_luminance_sreference_uwhite ??
    select(defaults.alumbra_token_luminance_sreference_uwhite, context, []);
  const alumbra_token_motion_sduration_sinstant =
    overrides.alumbra_token_motion_sduration_sinstant ??
    select(defaults.alumbra_token_motion_sduration_sinstant, context, []);
  const alumbra_token_motion_sduration_slong =
    overrides.alumbra_token_motion_sduration_slong ??
    select(defaults.alumbra_token_motion_sduration_slong, context, ["motion"]);
  const alumbra_token_motion_sduration_smedium =
    overrides.alumbra_token_motion_sduration_smedium ??
    select(defaults.alumbra_token_motion_sduration_smedium, context, ["motion"]);
  const alumbra_token_motion_sduration_sshort =
    overrides.alumbra_token_motion_sduration_sshort ??
    select(defaults.alumbra_token_motion_sduration_sshort, context, ["motion"]);
  const alumbra_token_motion_seasing_semphasized =
    overrides.alumbra_token_motion_seasing_semphasized ??
    select(defaults.alumbra_token_motion_seasing_semphasized, context, []);
  const alumbra_token_motion_seasing_sstandard =
    overrides.alumbra_token_motion_seasing_sstandard ??
    select(defaults.alumbra_token_motion_seasing_sstandard, context, []);
  const alumbra_token_opacity_sdisabled =
    overrides.alumbra_token_opacity_sdisabled ??
    select(defaults.alumbra_token_opacity_sdisabled, context, []);
  const alumbra_token_radius_slarge =
    overrides.alumbra_token_radius_slarge ??
    select(defaults.alumbra_token_radius_slarge, context, []);
  const alumbra_token_radius_smedium =
    overrides.alumbra_token_radius_smedium ??
    select(defaults.alumbra_token_radius_smedium, context, []);
  const alumbra_token_radius_spill =
    overrides.alumbra_token_radius_spill ??
    select(defaults.alumbra_token_radius_spill, context, []);
  const alumbra_token_radius_ssmall =
    overrides.alumbra_token_radius_ssmall ??
    select(defaults.alumbra_token_radius_ssmall, context, []);
  const alumbra_token_spacing_sextra_ularge =
    overrides.alumbra_token_spacing_sextra_ularge ??
    select(defaults.alumbra_token_spacing_sextra_ularge, context, ["density"]);
  const alumbra_token_spacing_slarge =
    overrides.alumbra_token_spacing_slarge ??
    select(defaults.alumbra_token_spacing_slarge, context, ["density"]);
  const alumbra_token_spacing_smedium =
    overrides.alumbra_token_spacing_smedium ??
    select(defaults.alumbra_token_spacing_smedium, context, ["density"]);
  const alumbra_token_spacing_snone =
    overrides.alumbra_token_spacing_snone ??
    select(defaults.alumbra_token_spacing_snone, context, []);
  const alumbra_token_spacing_ssmall =
    overrides.alumbra_token_spacing_ssmall ??
    select(defaults.alumbra_token_spacing_ssmall, context, ["density"]);
  const alumbra_token_spacing_stight =
    overrides.alumbra_token_spacing_stight ??
    select(defaults.alumbra_token_spacing_stight, context, ["density"]);
  const alumbra_token_target_sminimum =
    overrides.alumbra_token_target_sminimum ??
    select(defaults.alumbra_token_target_sminimum, context, ["density"]);
  const alumbra_token_typography_sfont_ufamily_sui =
    overrides.alumbra_token_typography_sfont_ufamily_sui ??
    select(defaults.alumbra_token_typography_sfont_ufamily_sui, context, []);
  const alumbra_token_typography_sfont_usize_sbody =
    overrides.alumbra_token_typography_sfont_usize_sbody ??
    select(defaults.alumbra_token_typography_sfont_usize_sbody, context, ["density"]);
  const alumbra_token_typography_sfont_usize_scaption =
    overrides.alumbra_token_typography_sfont_usize_scaption ??
    select(defaults.alumbra_token_typography_sfont_usize_scaption, context, ["density"]);
  const alumbra_token_typography_sfont_usize_sdisplay =
    overrides.alumbra_token_typography_sfont_usize_sdisplay ??
    select(defaults.alumbra_token_typography_sfont_usize_sdisplay, context, ["density"]);
  const alumbra_token_typography_sfont_usize_stitle =
    overrides.alumbra_token_typography_sfont_usize_stitle ??
    select(defaults.alumbra_token_typography_sfont_usize_stitle, context, ["density"]);
  const alumbra_token_typography_sfont_uweight_sbold =
    overrides.alumbra_token_typography_sfont_uweight_sbold ??
    select(defaults.alumbra_token_typography_sfont_uweight_sbold, context, []);
  const alumbra_token_typography_sfont_uweight_smedium =
    overrides.alumbra_token_typography_sfont_uweight_smedium ??
    select(defaults.alumbra_token_typography_sfont_uweight_smedium, context, []);
  const alumbra_token_typography_sfont_uweight_sregular =
    overrides.alumbra_token_typography_sfont_uweight_sregular ??
    select(defaults.alumbra_token_typography_sfont_uweight_sregular, context, []);
  const alumbra_token_typography_sline_uheight_sbody =
    overrides.alumbra_token_typography_sline_uheight_sbody ??
    select(defaults.alumbra_token_typography_sline_uheight_sbody, context, []);
  const alumbra_token_typography_sline_uheight_scompact =
    overrides.alumbra_token_typography_sline_uheight_scompact ??
    select(defaults.alumbra_token_typography_sline_uheight_scompact, context, []);
  const alumbra_token_player_sad_sband_sbackground =
    overrides.alumbra_token_player_sad_sband_sbackground ??
    alumbra_token_color_ssurface_selevated;
  const alumbra_token_player_sad_uband_sbackground =
    overrides.alumbra_token_player_sad_uband_sbackground ??
    alumbra_token_color_ssurface_soverlay;
  const alumbra_token_player_sad_uchrome_sbadge_sbackground =
    overrides.alumbra_token_player_sad_uchrome_sbadge_sbackground ??
    alumbra_token_color_scontrol_sbackground;
  const alumbra_token_player_sbutton_sbackground =
    overrides.alumbra_token_player_sbutton_sbackground ??
    alumbra_token_color_scontrol_sbackground;
  const alumbra_token_player_sbutton_sbackground_sdisabled =
    overrides.alumbra_token_player_sbutton_sbackground_sdisabled ??
    alumbra_token_color_scontrol_sdisabled;
  const alumbra_token_player_sbutton_sbackground_sfocus =
    overrides.alumbra_token_player_sbutton_sbackground_sfocus ??
    alumbra_token_color_scontrol_sbackground;
  const alumbra_token_player_sbutton_sbackground_shover =
    overrides.alumbra_token_player_sbutton_sbackground_shover ??
    alumbra_token_color_scontrol_shover;
  const alumbra_token_player_sbutton_sbackground_spressed =
    overrides.alumbra_token_player_sbutton_sbackground_spressed ??
    alumbra_token_color_scontrol_spressed;
  const alumbra_token_player_sbutton_sforeground =
    overrides.alumbra_token_player_sbutton_sforeground ??
    alumbra_token_color_scontrol_sforeground;
  const alumbra_token_player_sbutton_sforeground_sdisabled =
    overrides.alumbra_token_player_sbutton_sforeground_sdisabled ??
    alumbra_token_color_stext_sdisabled;
  const alumbra_token_player_sbutton_sforeground_sfocus =
    overrides.alumbra_token_player_sbutton_sforeground_sfocus ??
    alumbra_token_color_scontrol_sforeground;
  const alumbra_token_player_sbutton_sforeground_shover =
    overrides.alumbra_token_player_sbutton_sforeground_shover ??
    alumbra_token_color_scontrol_sforeground;
  const alumbra_token_player_sbutton_sforeground_spressed =
    overrides.alumbra_token_player_sbutton_sforeground_spressed ??
    alumbra_token_color_scontrol_sforeground;
  const alumbra_token_player_sbutton_sradius =
    overrides.alumbra_token_player_sbutton_sradius ??
    alumbra_token_radius_smedium;
  const alumbra_token_player_scaptions_sfont_usize =
    overrides.alumbra_token_player_scaptions_sfont_usize ??
    alumbra_token_typography_sfont_usize_scaption;
  const alumbra_token_player_scaptions_stext =
    overrides.alumbra_token_player_scaptions_stext ??
    alumbra_token_color_stext_sprimary;
  const alumbra_token_player_scaptions_swindow =
    overrides.alumbra_token_player_scaptions_swindow ??
    alumbra_token_color_ssurface_sscrim;
  const alumbra_token_player_sfocus_sring =
    overrides.alumbra_token_player_sfocus_sring ??
    alumbra_token_color_sfocus_sring;
  const alumbra_token_player_sseekbar_splayed =
    overrides.alumbra_token_player_sseekbar_splayed ??
    alumbra_token_color_saccent_splayed;
  const alumbra_token_player_sseekbar_strack =
    overrides.alumbra_token_player_sseekbar_strack ??
    alumbra_token_color_sborder_ssubtle;
  return {
    alumbra_token_color_saccent_splayed,
    alumbra_token_color_sborder_ssubtle,
    alumbra_token_color_scontrol_sbackground,
    alumbra_token_color_scontrol_sdisabled,
    alumbra_token_color_scontrol_sforeground,
    alumbra_token_color_scontrol_shover,
    alumbra_token_color_scontrol_spressed,
    alumbra_token_color_sfocus_sring,
    alumbra_token_color_ssurface_sbase,
    alumbra_token_color_ssurface_selevated,
    alumbra_token_color_ssurface_soverlay,
    alumbra_token_color_ssurface_sscrim,
    alumbra_token_color_stext_sdisabled,
    alumbra_token_color_stext_sprimary,
    alumbra_token_color_stext_ssecondary,
    alumbra_token_elevation_smodal,
    alumbra_token_elevation_spanel,
    alumbra_token_elevation_sraised,
    alumbra_token_layer_sad_uchrome,
    alumbra_token_layer_sad_usdk_ucontainer,
    alumbra_token_layer_scaptions,
    alumbra_token_layer_scontrols,
    alumbra_token_layer_svideo_usurface,
    alumbra_token_luminance_shdr_uhighlight_uceiling,
    alumbra_token_luminance_sreference_uwhite,
    alumbra_token_motion_sduration_sinstant,
    alumbra_token_motion_sduration_slong,
    alumbra_token_motion_sduration_smedium,
    alumbra_token_motion_sduration_sshort,
    alumbra_token_motion_seasing_semphasized,
    alumbra_token_motion_seasing_sstandard,
    alumbra_token_opacity_sdisabled,
    alumbra_token_radius_slarge,
    alumbra_token_radius_smedium,
    alumbra_token_radius_spill,
    alumbra_token_radius_ssmall,
    alumbra_token_spacing_sextra_ularge,
    alumbra_token_spacing_slarge,
    alumbra_token_spacing_smedium,
    alumbra_token_spacing_snone,
    alumbra_token_spacing_ssmall,
    alumbra_token_spacing_stight,
    alumbra_token_target_sminimum,
    alumbra_token_typography_sfont_ufamily_sui,
    alumbra_token_typography_sfont_usize_sbody,
    alumbra_token_typography_sfont_usize_scaption,
    alumbra_token_typography_sfont_usize_sdisplay,
    alumbra_token_typography_sfont_usize_stitle,
    alumbra_token_typography_sfont_uweight_sbold,
    alumbra_token_typography_sfont_uweight_smedium,
    alumbra_token_typography_sfont_uweight_sregular,
    alumbra_token_typography_sline_uheight_sbody,
    alumbra_token_typography_sline_uheight_scompact,
    alumbra_token_player_sad_sband_sbackground,
    alumbra_token_player_sad_uband_sbackground,
    alumbra_token_player_sad_uchrome_sbadge_sbackground,
    alumbra_token_player_sbutton_sbackground,
    alumbra_token_player_sbutton_sbackground_sdisabled,
    alumbra_token_player_sbutton_sbackground_sfocus,
    alumbra_token_player_sbutton_sbackground_shover,
    alumbra_token_player_sbutton_sbackground_spressed,
    alumbra_token_player_sbutton_sforeground,
    alumbra_token_player_sbutton_sforeground_sdisabled,
    alumbra_token_player_sbutton_sforeground_sfocus,
    alumbra_token_player_sbutton_sforeground_shover,
    alumbra_token_player_sbutton_sforeground_spressed,
    alumbra_token_player_sbutton_sradius,
    alumbra_token_player_scaptions_sfont_usize,
    alumbra_token_player_scaptions_stext,
    alumbra_token_player_scaptions_swindow,
    alumbra_token_player_sfocus_sring,
    alumbra_token_player_sseekbar_splayed,
    alumbra_token_player_sseekbar_strack,
  };
}
