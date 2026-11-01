# Penumbra — the porting layer

Alumbra's equivalent of Cobalt's Starboard: a narrow contract that each platform implements so that
[Umbra](../umbra/) can run unchanged everywhere.

| Backend | Decode | Render / audio | DRM | Ads |
|---------|--------|----------------|-----|-----|
| `desktop/` | FFmpeg (LGPL), dav1d | SDL3 | ClearKey | built-in VAST/VMAP |
| `web/` | MSE, WebCodecs | WebGL, Web Audio | EME | IMA HTML5 |
| `android/` | MediaCodec | Surface, AAudio | MediaDrm | IMA Android |
| `apple/` | VideoToolbox | AVSampleBuffer* (per D-20) | FairPlay | IMA iOS/tvOS |

The contract is decided by [ADR-0016](../docs/adr/0016-penumbra-port-contract.md): a push-model player, with a
shared reference implementation built from decoders and sinks. The [v1 port contract](../docs/architecture/port-contract.md)
documents every call's sequence, lifetime, ownership and error rules, compatibility and reserved decisions.
Installed value types remain standard C++20-only; the in-tree Status-based abstract interfaces are non-installed.
Ports never depend on each other.
