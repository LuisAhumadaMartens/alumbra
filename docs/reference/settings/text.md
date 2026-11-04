# Settings: text

> **Generated file. Do not edit.** This page is generated from `settings/schema/*.toml` by `tools/settings/generate.py`. Change the schema and run `python3 tools/settings/generate.py`; hand edits are overwritten and fail the drift check.

19 settings in the `text` namespace. See the [settings reference index](README.md).

<a id="text.ass.enabled"></a>

## `text.ass.enabled`

Enable rendering of the supported ASS and SSA subset in Alumbra's own renderer.

| Field | Value |
|-------|-------|
| Type | `boolean` |
| Default | `true` |
| Explicit state | Whether the user supplied a value |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="text.auto_select"></a>

## `text.auto_select`

Policy used to choose text tracks when a media resource exposes tracks.

| Field | Value |
|-------|-------|
| Type | `enum` |
| Default | `"forced"` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

| Value | Meaning |
|-------|---------|
| `none` | Do not select a text track automatically. |
| `forced` | Select forced tracks when available. |
| `preferred_language` | Prefer tracks matching the configured language. |

<a id="text.background_colour"></a>

## `text.background_colour`

Preferred caption background colour in #RRGGBB form. An unset value preserves the author's colour.

| Field | Value |
|-------|-------|
| Type | `string` |
| Default | `"#000000"` |
| Explicit state | Whether the user supplied a value |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="text.background_opacity_percent"></a>

## `text.background_opacity_percent`

Preferred caption background opacity from transparent to opaque. The default is unset so author styling applies.

| Field | Value |
|-------|-------|
| Type | `integer` |
| Default | `unset` |
| Explicit state | Whether the user supplied a value |
| Range | `0` to `100` |
| Unit | `percent` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="text.charset.auto_detect"></a>

## `text.charset.auto_detect`

Detect the character encoding of text subtitle files when no byte order mark is present.

| Field | Value |
|-------|-------|
| Type | `boolean` |
| Default | `true` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="text.cue_memory_budget_kb"></a>

## `text.cue_memory_budget_kb`

Maximum in-memory text cue payload retained per player.

| Field | Value |
|-------|-------|
| Type | `integer` |
| Default | `1024` |
| Range | `64` to `65536` |
| Unit | `kb` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="text.edge"></a>

## `text.edge`

Preferred edge effect around caption glyphs. An unset value preserves the author's edge style.

| Field | Value |
|-------|-------|
| Type | `enum` |
| Default | `"none"` |
| Explicit state | Whether the user supplied a value |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |

| Value | Meaning |
|-------|---------|
| `none` | Draw no edge effect. |
| `raised` | Draw a raised edge effect. |
| `depressed` | Draw a depressed edge effect. |
| `uniform` | Draw a uniform outline. |
| `drop_shadow` | Draw a drop shadow. |

<a id="text.font_family"></a>

## `text.font_family`

Preferred caption font family. An unset value preserves the author's font family.

| Field | Value |
|-------|-------|
| Type | `enum` |
| Default | `"default"` |
| Explicit state | Whether the user supplied a value |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |

| Value | Meaning |
|-------|---------|
| `default` | Use the platform default caption font. |
| `monospace_serif` | Use a monospace serif font. |
| `proportional_serif` | Use a proportional serif font. |
| `monospace_sans` | Use a monospace sans-serif font. |
| `proportional_sans` | Use a proportional sans-serif font. |
| `casual` | Use a casual font. |
| `cursive` | Use a cursive font. |
| `small_caps` | Use a small-caps font. |

<a id="text.font_size_percent"></a>

## `text.font_size_percent`

Caption font size as a percentage of the author's or default size. The default scale is 100 per cent.

| Field | Value |
|-------|-------|
| Type | `integer` |
| Default | `100` |
| Explicit state | Whether the user supplied a value |
| Range | `25` to `400` |
| Unit | `percent` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="text.position_offset_percent"></a>

## `text.position_offset_percent`

Vertical caption position adjustment from the default position, as a percentage of the display height. An unset value preserves the author's position.

| Field | Value |
|-------|-------|
| Type | `integer` |
| Default | `0` |
| Explicit state | Whether the user supplied a value |
| Range | `-50` to `50` |
| Unit | `percent` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="text.srt.fallback_encoding"></a>

## `text.srt.fallback_encoding`

Character encoding used for SubRip text when automatic detection cannot identify it.

| Field | Value |
|-------|-------|
| Type | `string` |
| Default | `"windows-1252"` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="text.style_source"></a>

## `text.style_source`

Select the source of caption preferences. The default uses available platform accessibility preferences and falls back to app settings.

| Field | Value |
|-------|-------|
| Type | `enum` |
| Default | `"platform"` |
| Explicit state | Whether the user supplied a value |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |

| Value | Meaning |
|-------|---------|
| `settings` | Use caption values supplied through Alumbra settings. |
| `platform` | Use platform accessibility preferences where available. |

<a id="text.text_colour"></a>

## `text.text_colour`

Preferred caption text colour in #RRGGBB form. An unset value preserves the author's colour.

| Field | Value |
|-------|-------|
| Type | `string` |
| Default | `"#FFFFFF"` |
| Explicit state | Whether the user supplied a value |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="text.text_opacity_percent"></a>

## `text.text_opacity_percent`

Preferred caption text opacity from transparent to opaque. An unset value preserves the author's opacity.

| Field | Value |
|-------|-------|
| Type | `integer` |
| Default | `100` |
| Explicit state | Whether the user supplied a value |
| Range | `0` to `100` |
| Unit | `percent` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="text.ttml.max_document_bytes"></a>

## `text.ttml.max_document_bytes`

Maximum size of a TTML text track document.

| Field | Value |
|-------|-------|
| Type | `integer` |
| Default | `4194304` |
| Range | `65536` to `33554432` |
| Unit | `bytes` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="text.webvtt.enable_style_blocks"></a>

## `text.webvtt.enable_style_blocks`

Whether WebVTT STYLE blocks are parsed.

| Field | Value |
|-------|-------|
| Type | `boolean` |
| Default | `true` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="text.webvtt.max_cue_text_bytes"></a>

## `text.webvtt.max_cue_text_bytes`

Maximum cue text accepted from a WebVTT cue.

| Field | Value |
|-------|-------|
| Type | `integer` |
| Default | `65536` |
| Range | `1024` to `1048576` |
| Unit | `bytes` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="text.window_colour"></a>

## `text.window_colour`

Preferred caption window colour in #RRGGBB form. An unset value preserves the author's colour.

| Field | Value |
|-------|-------|
| Type | `string` |
| Default | `"#000000"` |
| Explicit state | Whether the user supplied a value |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="text.window_opacity_percent"></a>

## `text.window_opacity_percent`

Preferred caption window opacity from transparent to opaque. An unset value preserves the author's opacity.

| Field | Value |
|-------|-------|
| Type | `integer` |
| Default | `0` |
| Explicit state | Whether the user supplied a value |
| Range | `0` to `100` |
| Unit | `percent` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |
