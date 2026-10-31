# 0004 — On the web, output through MSE by default and WebCodecs + WebGL as fallback

- **Status:** Accepted
- **Date:** 2026-10-31
- **Deciders:** @LuisAhumadaMartens

## Context

In browsers, Umbra runs as WebAssembly. It needs a way to get decoded media on screen and out of the speakers.

- **MSE path:** Umbra demuxes and (re)muxes to fMP4/CMAF and appends to a `SourceBuffer` on a native `<video>`.
  Hardware decoding, EME/DRM, Picture-in-Picture, AirPlay/Remote Playback, and the IMA HTML5 SDK all assume a real
  `<video>` element.
- **WebCodecs path:** Umbra decodes through `VideoDecoder`/`AudioDecoder` and presents frames via WebGL and Web
  Audio. Maximum control (any container, ASS rendered with libass, frame-accurate overlays), but no EME, and Umbra
  owns A/V sync, power usage and browser-specific quirks.

## Decision drivers

- DRM on the web is only possible through EME, which requires a media element.
- IMA and platform features expect a `<video>`.
- VLC-like "play anything" goal.

## Considered options

1. **Hybrid:** MSE by default, WebCodecs + WebGL as the "play anything" fallback, chosen per stream at load time.
2. **MSE only.**
3. **WebCodecs + canvas only.**

## Decision

Chosen option: **1 — Hybrid.** A selection step at load time picks the MSE path when the stream's container/codecs
can be expressed as MSE-compatible fMP4 and the browser reports support; otherwise it falls back to WebCodecs.
Encrypted content always uses MSE + EME.

## Consequences

**Positive**
- Hardware decode, DRM and ecosystem features by default; reach beyond browser-native formats when needed.

**Negative / costs**
- Two web output backends to build and test.
- iPhone Safari support for MSE (Managed Media Source) and WebCodecs varies by version; covered by D-22.

**Follow-ups**
- D-16 selection rule · D-17 WASM threading and COOP/COEP vs IMA iframes · D-22 compatibility matrix.

## References

- [Media Source Extensions](https://www.w3.org/TR/media-source-2/) · [WebCodecs](https://www.w3.org/TR/webcodecs/) ·
  [Encrypted Media Extensions](https://www.w3.org/TR/encrypted-media-2/)
