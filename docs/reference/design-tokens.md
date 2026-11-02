# Design token reference

Generated from [`design/tokens/`](../../design/tokens/) by `python3 tools/design/generate_tokens.py`;
do not edit it by hand. [Generated outputs](../../design/README.md#generated-outputs) explains the
output files, the unit mapping and colour handling.

## Contexts

| Modifier | Contexts | Default | CSS attribute | Media query that selects a context |
|----------|----------|---------|---------------|-------------------------------------|
| `theme` | `dark`, `light` | `dark` | `data-alumbra-theme` | `(prefers-color-scheme: light)` → `light` |
| `contrast` | `normal`, `high` | `normal` | `data-alumbra-contrast` | `(prefers-contrast: more)` → `high` |
| `density` | `phone`, `tablet`, `desktop`, `tv` | `desktop` | `data-alumbra-density` | none; set the attribute |
| `motion` | `full`, `reduced` | `full` | `data-alumbra-motion` | `(prefers-reduced-motion: reduce)` → `reduced` |
| `dynamic-range` | `sdr`, `hdr` | `sdr` | `data-alumbra-dynamic-range` | none; set the attribute |

## Tokens

Default values are for the default context. Primitive tokens exist only as CSS custom properties, where
aliases and contexts resolve them; renderers use semantic and component tokens.

| Token | Tier | Type | CSS custom property | Identifier | Alias of | Varies with | Default | Description |
|-------|------|------|---------------------|------------|----------|-------------|---------|-------------|
| `color.accent.played` | semantic | `color` | `--alumbra-color-accent-played` | `alumbra_token_color_saccent_splayed` |  | constant | #0ea5e9 | Accent color for the played seekbar track. |
| `color.blue.500` | primitive | `color` | `--alumbra-color-blue-500` | CSS only |  |  | #0ea5e9 | Blue palette shade 500 (#0ea5e9) for accent and interactive roles. |
| `color.blue.600` | primitive | `color` | `--alumbra-color-blue-600` | CSS only |  |  | #0284c7 | Blue palette shade 600 (#0284c7) for accent and interactive roles. |
| `color.blue.700` | primitive | `color` | `--alumbra-color-blue-700` | CSS only |  |  | #0369a1 | Blue palette shade 700 (#0369a1) for accent and interactive roles. |
| `color.border.subtle` | semantic | `color` | `--alumbra-color-border-subtle` | `alumbra_token_color_sborder_ssubtle` |  | `theme`, `contrast` | #64748b | Subtle border for controls and panels. |
| `color.control.background` | semantic | `color` | `--alumbra-color-control-background` | `alumbra_token_color_scontrol_sbackground` |  | `theme`, `contrast` | #1e293b | Default interactive control surface. |
| `color.control.disabled` | semantic | `color` | `--alumbra-color-control-disabled` | `alumbra_token_color_scontrol_sdisabled` |  | `theme`, `contrast` | #1e293b | Disabled control surface. |
| `color.control.foreground` | semantic | `color` | `--alumbra-color-control-foreground` | `alumbra_token_color_scontrol_sforeground` |  | `theme`, `contrast` | #ffffff | Control label and icon color on control.background. |
| `color.control.hover` | semantic | `color` | `--alumbra-color-control-hover` | `alumbra_token_color_scontrol_shover` |  | `theme`, `contrast` | #334155 | Hovered or D-pad highlighted control surface. |
| `color.control.pressed` | semantic | `color` | `--alumbra-color-control-pressed` | `alumbra_token_color_scontrol_spressed` |  | `theme`, `contrast` | #475569 | Pressed control surface. |
| `color.focus.ring` | semantic | `color` | `--alumbra-color-focus-ring` | `alumbra_token_color_sfocus_sring` |  | `theme`, `contrast` | #0ea5e9 | Visible keyboard and D-pad focus indicator on base and elevated surfaces. |
| `color.neutral.0` | primitive | `color` | `--alumbra-color-neutral-0` | CSS only |  |  | #ffffff | Neutral palette shade 0 (#ffffff) for role-based surfaces, text and borders. |
| `color.neutral.50` | primitive | `color` | `--alumbra-color-neutral-50` | CSS only |  |  | #f8fafc | Neutral palette shade 50 (#f8fafc) for role-based surfaces, text and borders. |
| `color.neutral.100` | primitive | `color` | `--alumbra-color-neutral-100` | CSS only |  |  | #f1f5f9 | Neutral palette shade 100 (#f1f5f9) for role-based surfaces, text and borders. |
| `color.neutral.200` | primitive | `color` | `--alumbra-color-neutral-200` | CSS only |  |  | #e2e8f0 | Neutral palette shade 200 (#e2e8f0) for role-based surfaces, text and borders. |
| `color.neutral.300` | primitive | `color` | `--alumbra-color-neutral-300` | CSS only |  |  | #cbd5e1 | Neutral palette shade 300 (#cbd5e1) for role-based surfaces, text and borders. |
| `color.neutral.400` | primitive | `color` | `--alumbra-color-neutral-400` | CSS only |  |  | #94a3b8 | Neutral palette shade 400 (#94a3b8) for role-based surfaces, text and borders. |
| `color.neutral.500` | primitive | `color` | `--alumbra-color-neutral-500` | CSS only |  |  | #64748b | Neutral palette shade 500 (#64748b) for role-based surfaces, text and borders. |
| `color.neutral.600` | primitive | `color` | `--alumbra-color-neutral-600` | CSS only |  |  | #475569 | Neutral palette shade 600 (#475569) for role-based surfaces, text and borders. |
| `color.neutral.700` | primitive | `color` | `--alumbra-color-neutral-700` | CSS only |  |  | #334155 | Neutral palette shade 700 (#334155) for role-based surfaces, text and borders. |
| `color.neutral.800` | primitive | `color` | `--alumbra-color-neutral-800` | CSS only |  |  | #1e293b | Neutral palette shade 800 (#1e293b) for role-based surfaces, text and borders. |
| `color.neutral.900` | primitive | `color` | `--alumbra-color-neutral-900` | CSS only |  |  | #0f172a | Neutral palette shade 900 (#0f172a) for role-based surfaces, text and borders. |
| `color.neutral.950` | primitive | `color` | `--alumbra-color-neutral-950` | CSS only |  |  | #090e18 | Neutral palette shade 950 (#090e18) for role-based surfaces, text and borders. |
| `color.neutral.1000` | primitive | `color` | `--alumbra-color-neutral-1000` | CSS only |  |  | #000000 | Neutral palette shade 1000 (#000000) for role-based surfaces, text and borders. |
| `color.scheme.high.border` | primitive | `color` | `--alumbra-color-scheme-high-border` | CSS only |  |  | #ffffff | Default dark-theme high-contrast control and panel border color. |
| `color.scheme.high.control` | primitive | `color` | `--alumbra-color-scheme-high-control` | CSS only |  |  | #000000 | Default dark-theme high-contrast control background color. |
| `color.scheme.high.control_disabled` | primitive | `color` | `--alumbra-color-scheme-high-control_disabled` | CSS only |  |  | #000000 | Default dark-theme high-contrast disabled control background. |
| `color.scheme.high.control_foreground` | primitive | `color` | `--alumbra-color-scheme-high-control_foreground` | CSS only |  |  | #ffffff | Default dark-theme high-contrast control label and icon color. |
| `color.scheme.high.control_hover` | primitive | `color` | `--alumbra-color-scheme-high-control_hover` | CSS only |  |  | #0f172a | Default dark-theme high-contrast hovered or D-pad highlighted control background. |
| `color.scheme.high.control_pressed` | primitive | `color` | `--alumbra-color-scheme-high-control_pressed` | CSS only |  |  | #1e293b | Default dark-theme high-contrast pressed control background. |
| `color.scheme.high.focus` | primitive | `color` | `--alumbra-color-scheme-high-focus` | CSS only |  |  | #ffff00 | Default dark-theme high-contrast keyboard and D-pad focus-ring color. |
| `color.scheme.high.surface` | primitive | `color` | `--alumbra-color-scheme-high-surface` | CSS only |  |  | #000000 | Default dark-theme high-contrast base surface color. |
| `color.scheme.high.surface_elevated` | primitive | `color` | `--alumbra-color-scheme-high-surface_elevated` | CSS only |  |  | #000000 | Default dark-theme high-contrast elevated surface color. |
| `color.scheme.high.surface_overlay` | primitive | `color` | `--alumbra-color-scheme-high-surface_overlay` | CSS only |  |  | #000000 | Default dark-theme high-contrast overlay surface color. |
| `color.scheme.high.text_primary` | primitive | `color` | `--alumbra-color-scheme-high-text_primary` | CSS only |  |  | #ffffff | Default dark-theme high-contrast primary text color. |
| `color.scheme.high.text_secondary` | primitive | `color` | `--alumbra-color-scheme-high-text_secondary` | CSS only |  |  | #ffffff | Default dark-theme high-contrast secondary text color. |
| `color.scheme.normal.border` | primitive | `color` | `--alumbra-color-scheme-normal-border` | CSS only |  |  | #64748b | Default dark-theme normal-contrast control and panel border color. |
| `color.scheme.normal.control` | primitive | `color` | `--alumbra-color-scheme-normal-control` | CSS only |  |  | #1e293b | Default dark-theme normal-contrast control background color. |
| `color.scheme.normal.control_disabled` | primitive | `color` | `--alumbra-color-scheme-normal-control_disabled` | CSS only |  |  | #1e293b | Default dark-theme normal-contrast disabled control background. |
| `color.scheme.normal.control_foreground` | primitive | `color` | `--alumbra-color-scheme-normal-control_foreground` | CSS only |  |  | #ffffff | Default dark-theme normal-contrast control label and icon color. |
| `color.scheme.normal.control_hover` | primitive | `color` | `--alumbra-color-scheme-normal-control_hover` | CSS only |  |  | #334155 | Default dark-theme normal-contrast hovered or D-pad highlighted control background. |
| `color.scheme.normal.control_pressed` | primitive | `color` | `--alumbra-color-scheme-normal-control_pressed` | CSS only |  |  | #475569 | Default dark-theme normal-contrast pressed control background. |
| `color.scheme.normal.focus` | primitive | `color` | `--alumbra-color-scheme-normal-focus` | CSS only |  |  | #0ea5e9 | Default dark-theme normal-contrast keyboard and D-pad focus-ring color. |
| `color.scheme.normal.surface` | primitive | `color` | `--alumbra-color-scheme-normal-surface` | CSS only |  |  | #090e18 | Default dark-theme normal-contrast base surface color. |
| `color.scheme.normal.surface_elevated` | primitive | `color` | `--alumbra-color-scheme-normal-surface_elevated` | CSS only |  |  | #0f172a | Default dark-theme normal-contrast elevated surface color. |
| `color.scheme.normal.surface_overlay` | primitive | `color` | `--alumbra-color-scheme-normal-surface_overlay` | CSS only |  |  | #1e293b | Default dark-theme normal-contrast overlay surface color. |
| `color.scheme.normal.text_primary` | primitive | `color` | `--alumbra-color-scheme-normal-text_primary` | CSS only |  |  | #f8fafc | Default dark-theme normal-contrast primary text color. |
| `color.scheme.normal.text_secondary` | primitive | `color` | `--alumbra-color-scheme-normal-text_secondary` | CSS only |  |  | #cbd5e1 | Default dark-theme normal-contrast secondary text color. |
| `color.scrim.media` | primitive | `color` | `--alumbra-color-scrim-media` | CSS only |  |  | #000000 at 72% alpha | Composited over the current video frame; no fixed backdrop or contrast ratio is claimed. |
| `color.surface.base` | semantic | `color` | `--alumbra-color-surface-base` | `alumbra_token_color_ssurface_sbase` |  | `theme`, `contrast` | #090e18 | Primary player surface. |
| `color.surface.elevated` | semantic | `color` | `--alumbra-color-surface-elevated` | `alumbra_token_color_ssurface_selevated` |  | `theme`, `contrast` | #0f172a | Raised control and panel surface. |
| `color.surface.overlay` | semantic | `color` | `--alumbra-color-surface-overlay` | `alumbra_token_color_ssurface_soverlay` |  | `theme`, `contrast` | #1e293b | Top-level player overlay surface. |
| `color.surface.scrim` | semantic | `color` | `--alumbra-color-surface-scrim` | `alumbra_token_color_ssurface_sscrim` |  | constant | #000000 at 72% alpha | Translucent scrim composited over the current video frame. |
| `color.text.disabled` | semantic | `color` | `--alumbra-color-text-disabled` | `alumbra_token_color_stext_sdisabled` |  | constant | #64748b | Disabled text; excluded from the declared WCAG text pairs because disabled controls are exempt. |
| `color.text.primary` | semantic | `color` | `--alumbra-color-text-primary` | `alumbra_token_color_stext_sprimary` |  | `theme`, `contrast` | #f8fafc | Primary text on base and elevated surfaces. |
| `color.text.secondary` | semantic | `color` | `--alumbra-color-text-secondary` | `alumbra_token_color_stext_ssecondary` |  | `theme`, `contrast` | #cbd5e1 | Secondary text on base and elevated surfaces. |
| `elevation.level_1` | primitive | `shadow` | `--alumbra-elevation-level_1` | CSS only |  |  | 0px 2px 6px 0px #000000 | Base level 1 shadow for separating layered player surfaces. |
| `elevation.level_2` | primitive | `shadow` | `--alumbra-elevation-level_2` | CSS only |  |  | 0px 4px 12px 0px #000000 | Base level 2 shadow for separating layered player surfaces. |
| `elevation.level_3` | primitive | `shadow` | `--alumbra-elevation-level_3` | CSS only |  |  | 0px 8px 24px 0px #000000 | Base level 3 shadow for separating layered player surfaces. |
| `elevation.modal` | semantic | `shadow` | `--alumbra-elevation-modal` | `alumbra_token_elevation_smodal` |  | constant | 0px 8px 24px 0px #000000 | Modal overlay elevation. |
| `elevation.panel` | semantic | `shadow` | `--alumbra-elevation-panel` | `alumbra_token_elevation_spanel` |  | constant | 0px 4px 12px 0px #000000 | Panel elevation. |
| `elevation.raised` | semantic | `shadow` | `--alumbra-elevation-raised` | `alumbra_token_elevation_sraised` |  | constant | 0px 2px 6px 0px #000000 | Raised control elevation. |
| `layer.ad_chrome` | semantic | `number` | `--alumbra-layer-ad_chrome` | `alumbra_token_layer_sad_uchrome` |  | constant | 30 | Player-owned ad chrome above controls and below the ad SDK container. |
| `layer.ad_sdk_container` | semantic | `number` | `--alumbra-layer-ad_sdk_container` | `alumbra_token_layer_sad_usdk_ucontainer` |  | constant | 40 | Ad SDK container owns input during ad playback. |
| `layer.captions` | semantic | `number` | `--alumbra-layer-captions` | `alumbra_token_layer_scaptions` |  | constant | 10 | Text tracks above video and below player controls. |
| `layer.controls` | semantic | `number` | `--alumbra-layer-controls` | `alumbra_token_layer_scontrols` |  | constant | 20 | Player controls above text tracks. |
| `layer.stack.ad_chrome` | primitive | `number` | `--alumbra-layer-stack-ad_chrome` | CSS only |  |  | 30 | Base stacking order for ad chrome relative to other player layers. |
| `layer.stack.ad_sdk_container` | primitive | `number` | `--alumbra-layer-stack-ad_sdk_container` | CSS only |  |  | 40 | Base stacking order for ad sdk container relative to other player layers. |
| `layer.stack.captions` | primitive | `number` | `--alumbra-layer-stack-captions` | CSS only |  |  | 10 | Base stacking order for captions relative to other player layers. |
| `layer.stack.controls` | primitive | `number` | `--alumbra-layer-stack-controls` | CSS only |  |  | 20 | Base stacking order for controls relative to other player layers. |
| `layer.stack.video_surface` | primitive | `number` | `--alumbra-layer-stack-video_surface` | CSS only |  |  | 0 | Base stacking order for video surface relative to other player layers. |
| `layer.video_surface` | semantic | `number` | `--alumbra-layer-video_surface` | `alumbra_token_layer_svideo_usurface` |  | constant | 0 | Bottom video or ad surface in the overlay stack. |
| `luminance.hdr_highlight_ceiling` | semantic | `number` | `--alumbra-luminance-hdr_highlight_ceiling` | `alumbra_token_luminance_shdr_uhighlight_uceiling` |  | `dynamic-range` | 203 | HDR highlight headroom in cd/m2; renderer mapping is staged. |
| `luminance.reference_white` | semantic | `number` | `--alumbra-luminance-reference_white` | `alumbra_token_luminance_sreference_uwhite` |  | constant | 203 | HDR UI reference white in cd/m2. |
| `luminance.value.hdr_highlight_ceiling` | primitive | `number` | `--alumbra-luminance-value-hdr_highlight_ceiling` | CSS only |  |  | 203 | SDR ceiling at reference white in cd/m2; HDR mode adds highlight headroom. |
| `luminance.value.reference_white` | primitive | `number` | `--alumbra-luminance-value-reference_white` | CSS only |  |  | 203 | HDR graphics reference-white luminance in cd/m2 per ITU-R BT.2408. |
| `motion.curve.emphasized` | primitive | `cubicBezier` | `--alumbra-motion-curve-emphasized` | CSS only |  |  | cubic-bezier(0.2, 0, 0, 1.4) | Base emphasized cubic Bézier easing curve for player transitions. |
| `motion.curve.standard` | primitive | `cubicBezier` | `--alumbra-motion-curve-standard` | CSS only |  |  | cubic-bezier(0.2, 0, 0, 1) | Base standard cubic Bézier easing curve for player transitions. |
| `motion.duration.instant` | semantic | `duration` | `--alumbra-motion-duration-instant` | `alumbra_token_motion_sduration_sinstant` |  | constant | 0 ms | No animation. |
| `motion.duration.long` | semantic | `duration` | `--alumbra-motion-duration-long` | `alumbra_token_motion_sduration_slong` |  | `motion` | 320 ms | Long emphasis transition. |
| `motion.duration.medium` | semantic | `duration` | `--alumbra-motion-duration-medium` | `alumbra_token_motion_sduration_smedium` |  | `motion` | 200 ms | Standard state transition. |
| `motion.duration.short` | semantic | `duration` | `--alumbra-motion-duration-short` | `alumbra_token_motion_sduration_sshort` |  | `motion` | 120 ms | Short state transition. |
| `motion.easing.emphasized` | semantic | `cubicBezier` | `--alumbra-motion-easing-emphasized` | `alumbra_token_motion_seasing_semphasized` |  | constant | cubic-bezier(0.2, 0, 0, 1.4) | Emphasized easing curve. |
| `motion.easing.standard` | semantic | `cubicBezier` | `--alumbra-motion-easing-standard` | `alumbra_token_motion_seasing_sstandard` |  | constant | cubic-bezier(0.2, 0, 0, 1) | Standard easing curve. |
| `motion.time.instant` | primitive | `duration` | `--alumbra-motion-time-instant` | CSS only |  |  | 0 ms | Base instant motion duration (0 ms) before reduced-motion overrides. |
| `motion.time.long` | primitive | `duration` | `--alumbra-motion-time-long` | CSS only |  |  | 320 ms | Base long motion duration (320 ms) before reduced-motion overrides. |
| `motion.time.medium` | primitive | `duration` | `--alumbra-motion-time-medium` | CSS only |  |  | 200 ms | Base medium motion duration (200 ms) before reduced-motion overrides. |
| `motion.time.short` | primitive | `duration` | `--alumbra-motion-time-short` | CSS only |  |  | 120 ms | Base short motion duration (120 ms) before reduced-motion overrides. |
| `opacity.disabled` | semantic | `number` | `--alumbra-opacity-disabled` | `alumbra_token_opacity_sdisabled` |  | constant | 0.48 | Opacity for disabled controls. |
| `opacity.scale.disabled` | primitive | `number` | `--alumbra-opacity-scale-disabled` | CSS only |  |  | 0.48 | Base opacity for disabled player controls. |
| `player.ad.band.background` | component | `color` | `--alumbra-player-ad-band-background` | `alumbra_token_player_sad_sband_sbackground` | `color.surface.elevated` | alias | #0f172a | Distinct reserved ad band path used to prove injective output-name mapping. |
| `player.ad_band.background` | component | `color` | `--alumbra-player-ad_band-background` | `alumbra_token_player_sad_uband_sbackground` | `color.surface.overlay` | alias | #1e293b | Reserved ad-band surface; path mapping example preserving underscores. |
| `player.ad_chrome.badge.background` | component | `color` | `--alumbra-player-ad_chrome-badge-background` | `alumbra_token_player_sad_uchrome_sbadge_sbackground` | `color.control.background` | alias | #1e293b | Reserved surface for player-owned ad chrome in M5. |
| `player.button.background` | component | `color` | `--alumbra-player-button-background` | `alumbra_token_player_sbutton_sbackground` | `color.control.background` | alias | #1e293b | Default player button surface. |
| `player.button.background.disabled` | component | `color` | `--alumbra-player-button-background-disabled` | `alumbra_token_player_sbutton_sbackground_sdisabled` | `color.control.disabled` | alias | #1e293b | Disabled button surface. |
| `player.button.background.focus` | component | `color` | `--alumbra-player-button-background-focus` | `alumbra_token_player_sbutton_sbackground_sfocus` | `color.control.background` | alias | #1e293b | Focused button fill; the separate player.focus.ring token supplies the focus indicator. |
| `player.button.background.hover` | component | `color` | `--alumbra-player-button-background-hover` | `alumbra_token_player_sbutton_sbackground_shover` | `color.control.hover` | alias | #334155 | Hovered or D-pad highlighted button surface. |
| `player.button.background.pressed` | component | `color` | `--alumbra-player-button-background-pressed` | `alumbra_token_player_sbutton_sbackground_spressed` | `color.control.pressed` | alias | #475569 | Pressed button surface. |
| `player.button.foreground` | component | `color` | `--alumbra-player-button-foreground` | `alumbra_token_player_sbutton_sforeground` | `color.control.foreground` | alias | #ffffff | Default player button label and icon on the default surface. |
| `player.button.foreground.disabled` | component | `color` | `--alumbra-player-button-foreground-disabled` | `alumbra_token_player_sbutton_sforeground_sdisabled` | `color.text.disabled` | alias | #64748b | Disabled player button label and icon; disabled controls are exempt from the enabled-state contrast threshold. |
| `player.button.foreground.focus` | component | `color` | `--alumbra-player-button-foreground-focus` | `alumbra_token_player_sbutton_sforeground_sfocus` | `color.control.foreground` | alias | #ffffff | Focused player button label and icon on the focused button fill. |
| `player.button.foreground.hover` | component | `color` | `--alumbra-player-button-foreground-hover` | `alumbra_token_player_sbutton_sforeground_shover` | `color.control.foreground` | alias | #ffffff | Hovered or D-pad highlighted player button label and icon. |
| `player.button.foreground.pressed` | component | `color` | `--alumbra-player-button-foreground-pressed` | `alumbra_token_player_sbutton_sforeground_spressed` | `color.control.foreground` | alias | #ffffff | Pressed player button label and icon. |
| `player.button.radius` | component | `dimension` | `--alumbra-player-button-radius` | `alumbra_token_player_sbutton_sradius` | `radius.medium` | alias | 8 px | Player button corner radius. |
| `player.captions.font_size` | component | `dimension` | `--alumbra-player-captions-font_size` | `alumbra_token_player_scaptions_sfont_usize` | `typography.font_size.caption` | alias | 12 px | Caption text size; reserved for M3 user-adjustable styles. |
| `player.captions.text` | component | `color` | `--alumbra-player-captions-text` | `alumbra_token_player_scaptions_stext` | `color.text.primary` | alias | #f8fafc | Caption text foreground; reserved for the M3 caption-style milestone. |
| `player.captions.window` | component | `color` | `--alumbra-player-captions-window` | `alumbra_token_player_scaptions_swindow` | `color.surface.scrim` | alias | #000000 at 72% alpha | Caption background window; reserved for the M3 caption-style milestone. |
| `player.focus.ring` | component | `color` | `--alumbra-player-focus-ring` | `alumbra_token_player_sfocus_sring` | `color.focus.ring` | alias | #0ea5e9 | Visible keyboard and D-pad focus ring. |
| `player.seekbar.played` | component | `color` | `--alumbra-player-seekbar-played` | `alumbra_token_player_sseekbar_splayed` | `color.accent.played` | alias | #0ea5e9 | Played seekbar track color. |
| `player.seekbar.track` | component | `color` | `--alumbra-player-seekbar-track` | `alumbra_token_player_sseekbar_strack` | `color.border.subtle` | alias | #64748b | Unplayed seekbar track. |
| `radius.large` | semantic | `dimension` | `--alumbra-radius-large` | `alumbra_token_radius_slarge` |  | constant | 16 px | Large panel corner radius. |
| `radius.medium` | semantic | `dimension` | `--alumbra-radius-medium` | `alumbra_token_radius_smedium` |  | constant | 8 px | Standard corner radius. |
| `radius.pill` | semantic | `dimension` | `--alumbra-radius-pill` | `alumbra_token_radius_spill` |  | constant | 999 px | Fully rounded control radius. |
| `radius.scale.large` | primitive | `dimension` | `--alumbra-radius-scale-large` | CSS only |  |  | 16 px | Base large corner radius (16 px). |
| `radius.scale.medium` | primitive | `dimension` | `--alumbra-radius-scale-medium` | CSS only |  |  | 8 px | Base medium corner radius (8 px). |
| `radius.scale.none` | primitive | `dimension` | `--alumbra-radius-scale-none` | CSS only |  |  | 0 px | Base none corner radius (0 px). |
| `radius.scale.pill` | primitive | `dimension` | `--alumbra-radius-scale-pill` | CSS only |  |  | 999 px | Base pill corner radius (999 px). |
| `radius.scale.small` | primitive | `dimension` | `--alumbra-radius-scale-small` | CSS only |  |  | 4 px | Base small corner radius (4 px). |
| `radius.small` | semantic | `dimension` | `--alumbra-radius-small` | `alumbra_token_radius_ssmall` |  | constant | 4 px | Small corner radius. |
| `space.0` | primitive | `dimension` | `--alumbra-space-0` | CSS only |  |  | 0 px | Base spacing scale step 0 (0 px). |
| `space.1` | primitive | `dimension` | `--alumbra-space-1` | CSS only |  |  | 4 px | Base spacing scale step 1 (4 px). |
| `space.2` | primitive | `dimension` | `--alumbra-space-2` | CSS only |  |  | 8 px | Base spacing scale step 2 (8 px). |
| `space.3` | primitive | `dimension` | `--alumbra-space-3` | CSS only |  |  | 12 px | Base spacing scale step 3 (12 px). |
| `space.4` | primitive | `dimension` | `--alumbra-space-4` | CSS only |  |  | 16 px | Base spacing scale step 4 (16 px). |
| `space.6` | primitive | `dimension` | `--alumbra-space-6` | CSS only |  |  | 24 px | Base spacing scale step 6 (24 px). |
| `space.8` | primitive | `dimension` | `--alumbra-space-8` | CSS only |  |  | 32 px | Base spacing scale step 8 (32 px). |
| `space.12` | primitive | `dimension` | `--alumbra-space-12` | CSS only |  |  | 48 px | Base spacing scale step 12 (48 px). |
| `spacing.extra_large` | semantic | `dimension` | `--alumbra-spacing-extra_large` | `alumbra_token_spacing_sextra_ularge` |  | `density` | 32 px | Extra large separation. |
| `spacing.large` | semantic | `dimension` | `--alumbra-spacing-large` | `alumbra_token_spacing_slarge` |  | `density` | 24 px | Large separation. |
| `spacing.medium` | semantic | `dimension` | `--alumbra-spacing-medium` | `alumbra_token_spacing_smedium` |  | `density` | 16 px | Standard separation. |
| `spacing.none` | semantic | `dimension` | `--alumbra-spacing-none` | `alumbra_token_spacing_snone` |  | constant | 0 px | No separation. |
| `spacing.small` | semantic | `dimension` | `--alumbra-spacing-small` | `alumbra_token_spacing_ssmall` |  | `density` | 8 px | Small separation. |
| `spacing.tight` | semantic | `dimension` | `--alumbra-spacing-tight` | `alumbra_token_spacing_stight` |  | `density` | 4 px | Tight separation. |
| `target.minimum` | semantic | `dimension` | `--alumbra-target-minimum` | `alumbra_token_target_sminimum` |  | `density` | 44 px | Minimum interactive target width and height for the active density context. |
| `target.size.minimum` | primitive | `dimension` | `--alumbra-target-size-minimum` | CSS only |  |  | 44 px | Minimum width and height for an interactive target in the desktop density context. |
| `typography.font_family.android` | primitive | `fontFamily` | `--alumbra-typography-font_family-android` | CSS only |  |  | Roboto | Android platform system font stack. |
| `typography.font_family.apple` | primitive | `fontFamily` | `--alumbra-typography-font_family-apple` | CSS only |  |  | SF Pro Text, SF Pro Display | Apple platform system font stack. |
| `typography.font_family.system_ui` | primitive | `fontFamily` | `--alumbra-typography-font_family-system_ui` | CSS only |  |  | system-ui | Platform-neutral system UI fallback; platform-specific stacks are staged for renderer work. |
| `typography.font_family.ui` | semantic | `fontFamily` | `--alumbra-typography-font_family-ui` | `alumbra_token_typography_sfont_ufamily_sui` |  | constant | system-ui | Platform-neutral system UI font family. |
| `typography.font_size.body` | semantic | `dimension` | `--alumbra-typography-font_size-body` | `alumbra_token_typography_sfont_usize_sbody` |  | `density` | 16 px | Default readable body size. |
| `typography.font_size.caption` | semantic | `dimension` | `--alumbra-typography-font_size-caption` | `alumbra_token_typography_sfont_usize_scaption` |  | `density` | 12 px | Caption and metadata size. |
| `typography.font_size.display` | semantic | `dimension` | `--alumbra-typography-font_size-display` | `alumbra_token_typography_sfont_usize_sdisplay` |  | `density` | 32 px | Prominent display size. |
| `typography.font_size.title` | semantic | `dimension` | `--alumbra-typography-font_size-title` | `alumbra_token_typography_sfont_usize_stitle` |  | `density` | 24 px | Player title size. |
| `typography.font_weight.bold` | semantic | `fontWeight` | `--alumbra-typography-font_weight-bold` | `alumbra_token_typography_sfont_uweight_sbold` |  | constant | 700 | Strong text weight. |
| `typography.font_weight.medium` | semantic | `fontWeight` | `--alumbra-typography-font_weight-medium` | `alumbra_token_typography_sfont_uweight_smedium` |  | constant | 500 | Emphasized text weight. |
| `typography.font_weight.regular` | semantic | `fontWeight` | `--alumbra-typography-font_weight-regular` | `alumbra_token_typography_sfont_uweight_sregular` |  | constant | 400 | Default text weight. |
| `typography.leading.body` | primitive | `number` | `--alumbra-typography-leading-body` | CSS only |  |  | 1.5 | Base body line-height multiplier for readable text layout. |
| `typography.leading.compact` | primitive | `number` | `--alumbra-typography-leading-compact` | CSS only |  |  | 1.2 | Base compact line-height multiplier for readable text layout. |
| `typography.line_height.body` | semantic | `number` | `--alumbra-typography-line_height-body` | `alumbra_token_typography_sline_uheight_sbody` |  | constant | 1.5 | Body line-height ratio. |
| `typography.line_height.compact` | semantic | `number` | `--alumbra-typography-line_height-compact` | `alumbra_token_typography_sline_uheight_scompact` |  | constant | 1.2 | Compact line-height ratio. |
| `typography.scale.body` | primitive | `dimension` | `--alumbra-typography-scale-body` | CSS only |  |  | 16 px | Base body text size (16 px) before density overrides. |
| `typography.scale.caption` | primitive | `dimension` | `--alumbra-typography-scale-caption` | CSS only |  |  | 12 px | Base caption text size (12 px) before density overrides. |
| `typography.scale.display` | primitive | `dimension` | `--alumbra-typography-scale-display` | CSS only |  |  | 32 px | Base display text size (32 px) before density overrides. |
| `typography.scale.title` | primitive | `dimension` | `--alumbra-typography-scale-title` | CSS only |  |  | 24 px | Base title text size (24 px) before density overrides. |
| `typography.weight.bold` | primitive | `fontWeight` | `--alumbra-typography-weight-bold` | CSS only |  |  | 700 | Base bold font weight (700) for the typography scale. |
| `typography.weight.medium` | primitive | `fontWeight` | `--alumbra-typography-weight-medium` | CSS only |  |  | 500 | Base medium font weight (500) for the typography scale. |
| `typography.weight.regular` | primitive | `fontWeight` | `--alumbra-typography-weight-regular` | CSS only |  |  | 400 | Base regular font weight (400) for the typography scale. |
| `typography.weight.semibold` | primitive | `fontWeight` | `--alumbra-typography-weight-semibold` | CSS only |  |  | 600 | Base semibold font weight (600) for the typography scale. |
