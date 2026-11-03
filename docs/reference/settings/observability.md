# Settings: observability

> **Generated file. Do not edit.** This page is generated from `settings/schema/*.toml` by `tools/settings/generate.py`. Change the schema and run `python3 tools/settings/generate.py`; hand edits are overwritten and fail the drift check.

3 settings in the `observability` namespace. See the [settings reference index](README.md).

<a id="observability.log.categories"></a>

## `observability.log.categories`

Runtime log category filter from ADR-0020's shared vocabulary. The default is an empty filter; the logging facade in #50 owns filter application.

| Field | Value |
|-------|-------|
| Type | `string_list` |
| Default | `[]` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Stable |
| Since | 0.1.0 |

| Value | Meaning |
|-------|---------|
| `net` | Network requests and transport. |
| `manifest` | Manifest parsing and updates. |
| `abr` | Adaptive bitrate selection. |
| `buffer` | Buffering decisions. |
| `demux` | Media demultiplexing. |
| `decode` | Decoder operation. |
| `render` | Video presentation. |
| `audio` | Audio playback. |
| `text` | Captions and subtitles. |
| `ads` | Advertisement playback. |
| `drm` | Content protection. |
| `settings` | Configuration validation and changes. |
| `player` | Player lifecycle and state. |
| `port` | Platform integration. |

<a id="observability.log.level"></a>

## `observability.log.level`

Minimum runtime log severity. Only further restricts severities retained by the build-time floor; enabling compiled-out severities requires rebuilding.

| Field | Value |
|-------|-------|
| Type | `enum` |
| Default | `"info"` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Stable |
| Since | 0.1.0 |

| Value | Meaning |
|-------|---------|
| `verbose` | Detailed hot-path diagnostics. |
| `debug` | Debugging diagnostics. |
| `info` | Informational events. |
| `warning` | Warnings about degraded behaviour. |
| `error` | Errors requiring attention. |

<a id="observability.trace.categories"></a>

## `observability.trace.categories`

Enabled trace categories from ADR-0020's shared vocabulary. An empty list disables tracing. Runtime settings cannot enable instrumentation compiled out of the artefact.

| Field | Value |
|-------|-------|
| Type | `string_list` |
| Default | `[]` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Stable |
| Since | 0.1.0 |

| Value | Meaning |
|-------|---------|
| `net` | Network requests and transport. |
| `manifest` | Manifest parsing and updates. |
| `abr` | Adaptive bitrate selection. |
| `buffer` | Buffering decisions. |
| `demux` | Media demultiplexing. |
| `decode` | Decoder operation. |
| `render` | Video presentation. |
| `audio` | Audio playback. |
| `text` | Captions and subtitles. |
| `ads` | Advertisement playback. |
| `drm` | Content protection. |
| `settings` | Configuration validation and changes. |
| `player` | Player lifecycle and state. |
| `port` | Platform integration. |
