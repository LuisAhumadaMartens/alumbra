# Penumbra — the porting layer

Alumbra's equivalent of Cobalt's Starboard: a narrow contract that each platform implements so that
[Umbra](../umbra/) can run unchanged everywhere.

| Backend | Decode | Render / audio | DRM | Ads |
|---------|--------|----------------|-----|-----|
| `desktop/` | FFmpeg (LGPL), dav1d | SDL3 | ClearKey | built-in VAST/VMAP |
| `web/` | MSE, WebCodecs | WebGL, Web Audio | EME | IMA HTML5 |
| `android/` | MediaCodec | Surface, AAudio | MediaDrm | IMA Android |
| `apple/` | VideoToolbox | AVSampleBuffer* (per D-20) | FairPlay | IMA iOS/tvOS |

The shape of the contract is open definition **D-04**. Ports never depend on each other.
