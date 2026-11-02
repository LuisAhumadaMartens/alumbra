# Settings: text

> **Generated file. Do not edit.** This page is generated from `settings/schema/*.toml` by `tools/settings/generate.py`. Change the schema and run `python3 tools/settings/generate.py`; hand edits are overwritten and fail the drift check.

4 settings in the `text` namespace. See the [settings reference index](README.md).

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
