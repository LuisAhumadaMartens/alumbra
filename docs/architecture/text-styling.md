# Text styling and caption preferences

Caption preferences are resolved independently for each field. An explicitly set app preference takes precedence over
the platform preference, which takes precedence over the author's cue value. If neither the app nor the platform has
set a field, the author's value remains effective. The settings runtime records whether each user value was set,
including when that value equals its compiled default.

## FCC display features

| 47 CFR 79.103(c) feature | Setting or platform field |
|---|---|
| Presentation | `text.enabled` / `CaptionPreferences::enabled` |
| Colour | `text.text_color` and `text.background_color` |
| Opacity | `text.text_opacity_percent`, `text.background_opacity_percent` and `text.window_opacity_percent` |
| Size | `text.font_size_percent` / `CaptionPreferences::font_size_scale` |
| Font | `text.font_family` / `CaptionPreferences::font_family` |
| Background | `text.background_color` and `text.background_opacity_percent` |
| Edge | `text.edge` / `CaptionPreferences::edge_style` |
| Window | `text.window_color` and `text.window_opacity_percent` |
| Vertical position | `text.position_percent` / `CaptionPreferences::position_offset_percent` |

The text colour and opacity fields together represent the FCC foreground colour setting. The background and window
have separate colour and opacity fields. Colour values in the settings schema use `#RRGGBB`; platform values use
three 8-bit RGB channels. Opacity values in the schema use percent, while Penumbra values use fractions.

## Platform adapters

`CaptionPreferenceProvider` exposes a synchronous snapshot query and a replaceable change observer. Each member of
`CaptionPreferences` is optional. A platform adapter leaves an unsupported or unset field empty, and sends a full
snapshot when any system preference changes.

On macOS, the adapter reads user-domain values from `MACaptionAppearance` and observes
`kMACaptionAppearanceSettingsChangedNotification`. It maps the display type to presentation, relative character
size to font scale, font descriptor style to the shared font family, foreground/background/window colours and
opacities to their matching fields, and text edge style to the shared edge enum. The notification causes a new
snapshot to be published while playback is active.

An Android adapter uses `CaptioningManager` and its captioning change listener. Apple mobile adapters use the
available `MACaptionAppearance` APIs and notification. They follow the same optional-field mapping and observer
lifetime rules. Platform adapters do not persist app preferences; the app owns persistence and writes the `text.*`
user settings.

The settings reference is generated from `settings/schema/text.toml`. The cue layout and renderer consume the
resolved values without reopening or reloading the text track. Their connection to the preference query and live
cue restyling depends on the cue model and TextTrack integration.
