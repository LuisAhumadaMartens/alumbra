# Settings: hls

> **Generated file. Do not edit.** This page is generated from `settings/schema/*.toml` by `tools/settings/generate.py`. Change the schema and run `python3 tools/settings/generate.py`; hand edits are overwritten and fail the drift check.

3 settings in the `hls` namespace. See the [settings reference index](README.md).

<a id="hls.live.stale_playlist_failover"></a>

## `hls.live.stale_playlist_failover`

Allows failover when a live media playlist remains unchanged for at least 1.5 target durations. The default is true.

| Field | Value |
|-------|-------|
| Type | `boolean` |
| Default | `true` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="hls.live.start_offset_ms"></a>

## `hls.live.start_offset_ms`

Distance in milliseconds from the live end when starting playback. The default -1 follows EXT-X-START, HOLD-BACK and three target durations, in that order.

| Field | Value |
|-------|-------|
| Type | `integer` |
| Default | `-1` |
| Range | `-1` to `3600000` |
| Unit | `ms` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="hls.parser.strict"></a>

## `hls.parser.strict`

Rejects HLS playlists that violate a SHOULD-level rule when enabled. The default is false.

| Field | Value |
|-------|-------|
| Type | `boolean` |
| Default | `false` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |
