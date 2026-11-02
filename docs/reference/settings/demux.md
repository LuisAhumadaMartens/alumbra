# Settings: demux

> **Generated file. Do not edit.** This page is generated from `settings/schema/*.toml` by `tools/settings/generate.py`. Change the schema and run `python3 tools/settings/generate.py`; hand edits are overwritten and fail the drift check.

2 settings in the `demux` namespace. See the [settings reference index](README.md).

<a id="demux.ffmpeg.mode"></a>

## `demux.ffmpeg.mode`

Behaviour of the FFmpeg demux fallback slot, ADR-0025. The default is `fallback`.

| Field | Value |
|-------|-------|
| Type | `enum` |
| Default | `"fallback"` |
| Platforms | Linux, macOS, Windows |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

| Value | Meaning |
|-------|---------|
| `fallback` | FFmpeg handles only what the own parsers do not. |
| `prefer` | FFmpeg handles every format it supports. |
| `off` | FFmpeg is never used. |

<a id="demux.mp4.edit_lists.enabled"></a>

## `demux.mp4.edit_lists.enabled`

Whether MP4 edit lists are honoured. The default of `true` follows the container specification. Replaces the proposed negative `demux.mp4.ignore_edit_lists`.

| Field | Value |
|-------|-------|
| Type | `boolean` |
| Default | `true` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |
