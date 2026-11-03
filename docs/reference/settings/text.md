# Settings: text

> **Generated file. Do not edit.** This page is generated from `settings/schema/*.toml` by `tools/settings/generate.py`. Change the schema and run `python3 tools/settings/generate.py`; hand edits are overwritten and fail the drift check.

11 settings in the `text` namespace. See the [settings reference index](README.md).

<a id="text.background_color"></a>

## `text.background_color`

Preferred caption background colour in #RRGGBB form. Author background colour remains effective until this field is explicitly set.

| Field | Value |
|-------|-------|
| Type | `string` |
| Default | `"#000000"` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="text.background_opacity_percent"></a>

## `text.background_opacity_percent`

Preferred caption background opacity, from 0 to 100 percent. The default is 75 percent; author opacity remains effective until this field is explicitly set.

| Field | Value |
|-------|-------|
| Type | `double` |
| Default | `75.0` |
| Range | `0.0` to `100.0` |
| Unit | `percent` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="text.edge"></a>

## `text.edge`

Preferred caption text edge treatment. Author edge styling remains effective until this field is explicitly set.

| Field | Value |
|-------|-------|
| Type | `enum` |
| Default | `"none"` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |

| Value | Meaning |
|-------|---------|
| `none` | No edge treatment. |
| `raised` | Raised edge treatment. |
| `depressed` | Depressed edge treatment. |
| `uniform` | Uniform outline. |
| `drop_shadow` | Drop shadow. |

<a id="text.enabled"></a>

## `text.enabled`

Whether the user wants available text captions presented. This is a user-selectable presentation preference and defaults to enabled.

| Field | Value |
|-------|-------|
| Type | `boolean` |
| Default | `true` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="text.font_family"></a>

## `text.font_family`

Preferred caption font family. The default uses the platform caption font; author font styling remains effective until this field is explicitly set.

| Field | Value |
|-------|-------|
| Type | `enum` |
| Default | `"default"` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |

| Value | Meaning |
|-------|---------|
| `default` | The platform default caption font. |
| `monospace_serif` | A monospaced serif font. |
| `proportional_serif` | A proportional serif font. |
| `monospace_sans` | A monospaced sans-serif font. |
| `proportional_sans` | A proportional sans-serif font. |
| `casual` | A casual display font. |
| `cursive` | A cursive font. |
| `small_caps` | A small-capitals font. |

<a id="text.font_size_percent"></a>

## `text.font_size_percent`

Caption font size relative to the renderer's default, in percent. The default is 100 percent; author sizing remains effective until this field is explicitly set.

| Field | Value |
|-------|-------|
| Type | `double` |
| Default | `100.0` |
| Range | `25.0` to `400.0` |
| Unit | `percent` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="text.position_percent"></a>

## `text.position_percent`

User vertical position adjustment in percent of the caption safe area, from -50 (up) to 50 (down). The default leaves author placement unchanged.

| Field | Value |
|-------|-------|
| Type | `integer` |
| Default | `0` |
| Range | `-50` to `50` |
| Unit | `percent` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="text.text_color"></a>

## `text.text_color`

Preferred caption foreground colour in #RRGGBB form. Author colour remains effective until this field is explicitly set.

| Field | Value |
|-------|-------|
| Type | `string` |
| Default | `"#FFFFFF"` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="text.text_opacity_percent"></a>

## `text.text_opacity_percent`

Preferred caption foreground opacity, from 0 to 100 percent. The default is fully opaque; author opacity remains effective until this field is explicitly set.

| Field | Value |
|-------|-------|
| Type | `double` |
| Default | `100.0` |
| Range | `0.0` to `100.0` |
| Unit | `percent` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="text.window_color"></a>

## `text.window_color`

Preferred caption window colour in #RRGGBB form. Author window colour remains effective until this field is explicitly set.

| Field | Value |
|-------|-------|
| Type | `string` |
| Default | `"#000000"` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="text.window_opacity_percent"></a>

## `text.window_opacity_percent`

Preferred caption window opacity, from 0 to 100 percent. The default is transparent; author opacity remains effective until this field is explicitly set.

| Field | Value |
|-------|-------|
| Type | `double` |
| Default | `0.0` |
| Range | `0.0` to `100.0` |
| Unit | `percent` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |
