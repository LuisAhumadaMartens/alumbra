# 0002 — Build one portable C++ engine behind a porting layer, exposing an HTMLMediaElement-shaped API

- **Status:** Accepted
- **Date:** 2026-10-31
- **Deciders:** @LuisAhumadaMartens

## Context

Alumbra must play the same media, with the same behaviour and settings, on Web, Android/Google TV,
iOS/iPadOS/tvOS and desktop. The usual industry answer is a *different player per platform* (hls.js/Shaka on the
web, ExoPlayer/Media3 on Android, AVPlayer on Apple), which multiplies bugs and diverges behaviour.

YouTube's living-room runtime, Cobalt, takes another route: a C++ runtime implements the HTML5 media contract
(`HTMLMediaElement`, MSE, EME) on top of **Starboard**, a narrow porting layer whose player interface
(**SbPlayer**) receives encoded samples and decodes/renders them on each device. In YouTube's case the streaming
logic (DASH, ABR) lives in the JavaScript application. See [research/cobalt-and-starboard.md](../research/cobalt-and-starboard.md).

## Decision drivers

- One behaviour everywhere; platform code limited to what is genuinely platform-specific.
- A learning and showcase goal centred on **native C++ media pipelines**.
- A public API that web developers already understand.
- VLC-like reach: also play sources browsers cannot (MKV, ASS, `rtmp`, `srt`, …).

## Considered options

1. **Portable C++ engine + Starboard-style porting layer + HTMLMediaElement-shaped API everywhere.** Streaming,
   ABR, demux, sync, text, ads core and settings in C++.
2. **Cobalt-faithful:** C++ implements only MSE/EME + an SbPlayer-like pipeline; HLS/DASH/ABR live in a
   TypeScript player on top.
3. **VLC-style native desktop player first;** web and mobile wrap it later.

## Decision

Chosen option: **1.** The engine (**Umbra**) owns networking orchestration, HLS/DASH (including low-latency
variants), ABR, demuxing, A/V sync, text tracks, the ad timeline, DRM session logic and settings. A narrow porting
layer (**Penumbra**) supplies decode, render, audio output, networking primitives, CDMs and ad SDK bridges. The
public API mirrors `HTMLMediaElement` (attributes, methods, events, error codes) on every platform, with MSE/EME
*semantics* used internally where they fit.

## Consequences

**Positive**
- Streaming behaviour is written, tested and fuzzed once.
- Clear, interview-relevant separation equivalent to Cobalt ↔ Starboard.
- Integrators get one API and one settings model.

**Negative / costs**
- On the web we re-implement what hls.js/Shaka already do, in C++/WASM — bundle size and startup must be
  watched (performance budgets in M11).
- Some platform features are only reachable through platform players (e.g. AVPlayer-only capabilities on Apple);
  this is handled by definition D-20.

**Follow-ups**
- D-03 public API surface · D-04 Penumbra contract shape · D-18 language bindings · D-20 Apple playback path.

## References

- [Cobalt repository](https://github.com/youtube/cobalt) · [Starboard porting guide](https://developers.google.com/youtube/cobalt/docs/starboard/porting)
- [HTML Standard — media elements](https://html.spec.whatwg.org/multipage/media.html) ·
  [Media Source Extensions](https://www.w3.org/TR/media-source-2/) · [Encrypted Media Extensions](https://www.w3.org/TR/encrypted-media-2/)
