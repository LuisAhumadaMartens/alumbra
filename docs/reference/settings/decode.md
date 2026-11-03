# Settings: decode

> **Generated file. Do not edit.** This page is generated from `settings/schema/*.toml` by `tools/settings/generate.py`. Change the schema and run `python3 tools/settings/generate.py`; hand edits are overwritten and fail the drift check.

9 settings in the `decode` namespace. See the [settings reference index](README.md).

<a id="decode.blocklist.extra_decoders"></a>

## `decode.blocklist.extra_decoders`

Decoder names to block, each optionally followed by a codec. The empty default adds none.

| Field | Value |
|-------|-------|
| Type | `string_list` |
| Default | `[]` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="decode.blocklist.ignored_entries"></a>

## `decode.blocklist.ignored_entries`

Built-in blocklist entry IDs to ignore. The empty default ignores none.

| Field | Value |
|-------|-------|
| Type | `string_list` |
| Default | `[]` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="decode.dropped_frames.max_percent"></a>

## `decode.dropped_frames.max_percent`

Share of frames due within the window that may be dropped late before decoder fallback is triggered. The default of 10 follows ADR-0029.

| Field | Value |
|-------|-------|
| Type | `integer` |
| Default | `10` |
| Range | `0` to `100` |
| Unit | `percent` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="decode.dropped_frames.window_ms"></a>

## `decode.dropped_frames.window_ms`

Playback window over which late drops are counted. The default of 5000 ms follows ADR-0029.

| Field | Value |
|-------|-------|
| Type | `duration` |
| Default | `5000` |
| Range | `1` to `9223372036854775807` |
| Unit | `ms` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="decode.excluded_codecs"></a>

## `decode.excluded_codecs`

Codecs whose decoders the desktop capability query reports as unsupported. The empty default excludes none. Read into `DecodeLimits::disabled_decoders`.

| Field | Value |
|-------|-------|
| Type | `string_list` |
| Default | `[]` |
| Platforms | Linux, macOS, Windows |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

| Value | Meaning |
|-------|---------|
| `h264` | H.264 video. |
| `hevc` | HEVC video. |
| `vp9` | VP9 video. |
| `vp8` | VP8 video. |
| `theora` | Theora video. |
| `av1` | AV1 video. |
| `aac` | AAC audio. |
| `opus` | Opus audio. |
| `vorbis` | Vorbis audio. |
| `ac3` | AC-3 audio. |
| `eac3` | E-AC-3 audio. |
| `flac` | FLAC audio. |
| `mp3` | MP3 audio. |

<a id="decode.fallback.enabled"></a>

## `decode.fallback.enabled`

Whether decoding moves to the next candidate after a failure. With false the first candidate's failure is reported, ADR-0029.

| Field | Value |
|-------|-------|
| Type | `boolean` |
| Default | `true` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="decode.prefer_hardware"></a>

## `decode.prefer_hardware`

Hardware decoder preference, ADR-0029. The default is `auto`.

| Field | Value |
|-------|-------|
| Type | `enum` |
| Default | `"auto"` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

| Value | Meaning |
|-------|---------|
| `auto` | The built-in order applies. |
| `always` | Hardware decoders are tried first and software is used only after they fail. |
| `never` | Software decoders only. |

<a id="decode.video.max_height"></a>

## `decode.video.max_height`

Tallest video, in pixels, that the desktop capability query accepts. Taller video is reported as unsupported and 0 means unlimited. Read into `DecodeLimits::max_video_height`. The unit is pixels but the key carries no suffix, see the settings README.

| Field | Value |
|-------|-------|
| Type | `integer` |
| Default | `0` |
| Range | `0` to `16384` |
| Platforms | Linux, macOS, Windows |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="decode.video.preferred_decoders"></a>

## `decode.video.preferred_decoders`

Decoder names that move to the front of the filtered candidates, in the given order. A name removed by capability filtering or the blocklist is not restored, ADR-0029. The empty default keeps the built-in order.

| Field | Value |
|-------|-------|
| Type | `string_list` |
| Default | `[]` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |
