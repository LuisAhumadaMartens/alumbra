# Settings: source

> **Generated file. Do not edit.** This page is generated from `settings/schema/*.toml` by `tools/settings/generate.py`. Change the schema and run `python3 tools/settings/generate.py`; hand edits are overwritten and fail the drift check.

3 settings in the `source` namespace. See the [settings reference index](README.md).

<a id="source.probe.max_bytes"></a>

## `source.probe.max_bytes`

Maximum bytes a progressive demuxer may inspect while establishing tracks and duration. The default is 2 MiB; the range is 4 KiB to 16 MiB. A seekable ISO-BMFF source may make one range read for a trailing moov box after the initial bytes identify the container. This limit is separate from the fixed 4 KiB to 64 KiB source-type sniffing budget.

| Field | Value |
|-------|-------|
| Type | `byte_size` |
| Default | `2097152` |
| Range | `4096` to `16777216` |
| Unit | `bytes` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="source.read_ahead_bytes"></a>

## `source.read_ahead_bytes`

Bytes the buffered reader keeps ahead of the last read position of a source. The default of 2 MiB follows #73. The reader asks the data source for more bytes only while fewer than this many are buffered ahead, so a progressive download pauses once the window is full.

| Field | Value |
|-------|-------|
| Type | `byte_size` |
| Default | `2097152` |
| Range | `65536` to `67108864` |
| Unit | `bytes` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="source.type_hint"></a>

## `source.type_hint`

Explicit media type of a source, set through the per-source overrides of ADR-0017. An explicit type is authoritative under ADR-0027: Umbra does not sniff, and the load fails if the bytes do not parse as that type. The empty default, or `auto`, detects the type from the first bytes of the response, with the extension and Content-Type only as tie-breakers. The accepted names are hls, dash, mp4, matroska, mpegts, adts, mp3 and flv.

| Field | Value |
|-------|-------|
| Type | `string` |
| Default | `""` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |
