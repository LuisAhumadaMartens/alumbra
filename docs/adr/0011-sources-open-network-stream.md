# 0011 — Accept any typed source URL, VLC "Open Network Stream" style

- **Status:** Accepted
- **Date:** 2026-10-31
- **Deciders:** @LuisAhumadaMartens

## Context

Like VLC, Alumbra should play anything a user can point at, simply by typing or pasting a URL. Supported schemes
differ by platform: browsers can only use HTTP(S) (and WebRTC), while native platforms can open raw transports.

## Decision

Alumbra's source resolver accepts a single URL string and determines the source type from the scheme, extension,
MIME type and content sniffing:

| Scheme | Meaning | Platforms |
|--------|---------|-----------|
| `http://`, `https://` | Progressive files, HLS (`.m3u8`), DASH (`.mpd`), low-latency variants | All |
| `file://` (and plain paths) | Local files | Desktop, Android, Apple |
| `rtmp://` | Real-Time Messaging Protocol | Native only |
| `rtsp://` | Real-Time Streaming Protocol | Native only |
| `srt://` | Secure Reliable Transport (the **SRT protocol**, not SubRip subtitles) | Native only |
| `udp://` | Raw UDP / multicast MPEG-TS | Native only |

Unsupported schemes on a platform fail fast with a clear `MediaError` and a documented reason.

## Consequences

- One input box in every demo app; behaviour documented in the [compatibility matrix](../compatibility.md).

**Follow-ups**
- D-14 protocol implementation (FFmpeg protocols vs dedicated libraries) and URL resolution rules.
