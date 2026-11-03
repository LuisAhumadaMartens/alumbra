# Settings: tracks

> **Generated file. Do not edit.** This page is generated from `settings/schema/*.toml` by `tools/settings/generate.py`. Change the schema and run `python3 tools/settings/generate.py`; hand edits are overwritten and fail the drift check.

2 settings in the `tracks` namespace. See the [settings reference index](README.md).

<a id="tracks.audio.prefer_descriptions"></a>

## `tracks.audio.prefer_descriptions`

Prefer audio description tracks when available. This preference is combined with the platform accessibility preference.

| Field | Value |
|-------|-------|
| Type | `boolean` |
| Default | `false` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="tracks.audio.preferred_languages"></a>

## `tracks.audio.preferred_languages`

Preferred audio language ranges in priority order, matched using RFC 4647 basic filtering. An empty list uses the platform locale.

| Field | Value |
|-------|-------|
| Type | `string_list` |
| Default | `[]` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |
