# Settings: perf

> **Generated file. Do not edit.** This page is generated from `settings/schema/*.toml` by `tools/settings/generate.py`. Change the schema and run `python3 tools/settings/generate.py`; hand edits are overwritten and fail the drift check.

2 settings in the `perf` namespace. See the [settings reference index](README.md).

<a id="perf.low_memory.max_video_height"></a>

## `perf.low_memory.max_video_height`

Maximum video rendition height in pixels under the low-memory profile. The 1080 px default is a provisional starting value pending measured device budgets.

| Field | Value |
|-------|-------|
| Type | `integer` |
| Default | `1080` |
| Range | `240` to `2160` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="perf.profile"></a>

## `perf.profile`

Memory and performance profile. Auto uses available platform memory signals and low-memory device flags; it selects standard when no low-memory signal is available.

| Field | Value |
|-------|-------|
| Type | `enum` |
| Default | `"auto"` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Restart |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

| Value | Meaning |
|-------|---------|
| `auto` | Select a profile from platform memory signals when available. |
| `standard` | Use standard buffering and rendition limits. |
| `low_memory` | Use reduced buffering and lower rendition limits for constrained devices. |
