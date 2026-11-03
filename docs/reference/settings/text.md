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

<a id="text.cea608.default_channel"></a>

## `text.cea608.default_channel`

CEA-608 caption channel selected by default when no channel preference is available.

| Field | Value |
|-------|-------|
| Type | `enum` |
| Default | `"cc1"` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

| Value | Meaning |
|-------|---------|
| `cc1` | Select CEA-608 channel CC1 by default. |
| `cc2` | Select CEA-608 channel CC2 by default. |
| `cc3` | Select CEA-608 channel CC3 by default. |
| `cc4` | Select CEA-608 channel CC4 by default. |

<a id="text.cea708.prefer_over_608"></a>

## `text.cea708.prefer_over_608`

Prefer a matching CEA-708 service over CEA-608 when both caption formats are available.

| Field | Value |
|-------|-------|
| Type | `boolean` |
| Default | `true` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
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
