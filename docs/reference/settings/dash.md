# Settings: dash

> **Generated file. Do not edit.** This page is generated from `settings/schema/*.toml` by `tools/settings/generate.py`. Change the schema and run `python3 tools/settings/generate.py`; hand edits are overwritten and fail the drift check.

3 settings in the `dash` namespace. See the [settings reference index](README.md).

<a id="dash.live.default_utc_timing_url"></a>

## `dash.live.default_utc_timing_url`

HTTP xsdate UTC source used when a dynamic DASH MPD has no UTCTiming entry. The default is empty, which uses the local clock.

| Field | Value |
|-------|-------|
| Type | `string` |
| Default | `""` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="dash.live.start_delay_ms"></a>

## `dash.live.start_delay_ms`

Distance in milliseconds behind the DASH live edge at start. The default -1 uses suggestedPresentationDelay, or three target segment durations when it is absent.

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

<a id="dash.mpd.max_bytes"></a>

## `dash.mpd.max_bytes`

Maximum DASH MPD input size accepted by the parser. The default is 16777216 bytes; values are limited to 65536 through 134217728 bytes.

| Field | Value |
|-------|-------|
| Type | `byte_size` |
| Default | `16777216` |
| Range | `65536` to `134217728` |
| Unit | `bytes` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |
