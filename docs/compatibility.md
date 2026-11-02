# Compatibility matrix

This matrix records Alumbra's target platforms and planned media compatibility. A support claim is only verified when the linked evidence meets its tier's cadence.

## Support floors

Tier 1 is verified in CI on every change, or on a named device on a fixed cadence where CI cannot run it. Tier 2 is verified on a reference device before each release. Tier 3 is best effort; defects do not block a release. Floors move only in a minor release.

| Platform | Floor | Tier | Evidence |
|---|---|---|---|
| Web, desktop | Chromium browsers and Firefox, current and previous (plus Firefox ESR); Safari 17.1 | 1 | [ADR-0039](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0039-compatibility-matrix-and-support-tiers.md) |
| Web, iPhone and iPad | Safari 17.1 (Safari 16.4 to 17.0 unsupported) | 1 | [ADR-0039](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0039-compatibility-matrix-and-support-tiers.md), [ADR-0004](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0004-web-output-hybrid-mse-webcodecs.md) |
| Web, Android | Chrome current; Samsung Internet and Firefox | 1; Samsung Internet and Firefox: 3 | [ADR-0039](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0039-compatibility-matrix-and-support-tiers.md) |
| Web, TV | Google TV and Android TV WebView; Tizen 8 and webOS 24 | 2; Tizen 8 and webOS 24: 3 | [ADR-0039](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0039-compatibility-matrix-and-support-tiers.md) |
| macOS | 14 | 1 | [ADR-0039](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0039-compatibility-matrix-and-support-tiers.md), [ADR-0013](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0013-cpp-language-standard.md) |
| Linux | Ubuntu 26.04 LTS (x86-64, arm64) and current Fedora; Wayland and X11 | 1 | [ADR-0039](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0039-compatibility-matrix-and-support-tiers.md), [ADR-0013](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0013-cpp-language-standard.md) |
| Windows | Windows 11 x64: engine builds, not supported | n/a | [ADR-0031](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0031-windows-support-scope-and-toolchain.md) |
| iOS, iPadOS, tvOS | 17 | 1 | [ADR-0039](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0039-compatibility-matrix-and-support-tiers.md) |
| Android / Google TV | minSdk 28 (Android 9); a move to 30 is pre-approved if 28 becomes a burden in M6 | 1 | [ADR-0039](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0039-compatibility-matrix-and-support-tiers.md) |

Web builds need WASM SIMD. Threads are optional ([ADR-0037](adr/0037-wasm-threading-and-cross-origin-isolation.md)). Toolchain floors are in [ADR-0013](adr/0013-cpp-language-standard.md).

## Sources and protocols

| Source | Desktop | Web | Android / Google TV | iOS / iPadOS / tvOS |
|---|---|---|---|---|
| Progressive MP4 / fMP4 | [● M1](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [● M4](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [● M6](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [● M7](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) |
| Progressive MPEG-TS | [● M1](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [● M4](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [● M6](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [● M7](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) |
| Progressive WebM / MKV | [● M1](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [◐ M4 ¹](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [● M6](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [● M7](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) |
| HLS (TS, fMP4/CMAF) | [● M2](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [● M4](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [● M6](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [● M7](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) |
| DASH | [● M2](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [● M4](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [● M6](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [● M7](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) |
| LL-HLS | [● M8](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [● M8](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [● M8](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [● M8](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) |
| LL-DASH | [● M8](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [● M8](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [● M8](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [● M8](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) |
| rtmp://, rtsp://, srt://, udp:// | [● M8](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | — ² | [● M8](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [◐ M8 ³](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) |

¹ Via the WebCodecs fallback when MSE cannot express the stream.
² Browsers expose no raw sockets; WebRTC/WHEP is evaluated in D-14.
³ Subject to App Store networking and licensing constraints (D-32).

## Codecs

| Codec | Desktop (FFmpeg/dav1d) | Web (MSE/WebCodecs) | Android (MediaCodec) | Apple (VideoToolbox/AudioToolbox) |
|---|---|---|---|---|
| H.264 / AVC | [● M1](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [● M4](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [● M6](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [● M7](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) |
| H.265 / HEVC | [● M1](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [◐ browser-dependent](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [◐ device-dependent](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [● M7](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) |
| VP9 | [● M1](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [◐ browser-dependent](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [◐ device-dependent](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [◐ device-dependent](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) |
| AV1 | [● M1](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [◐ browser-dependent](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [◐ device-dependent](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [◐ device-dependent](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) |
| AAC | [● M1](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [● M4](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [● M6](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [● M7](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) |
| Opus | [● M1](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [● M4](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [● M6](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [◐](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) |
| AC-3 / E-AC-3 | [● M1](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [◐](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [◐ passthrough/decoder](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [● M7](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) |
| FLAC, MP3 | [● M1](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [● M4](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [● M6](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [● M7](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) |
| HDR10 / HLG | [◐ M11](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [◐ M11](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [◐ M11](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) | [◐ M11](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) |

Device- and browser-dependent cells are resolved at runtime by capability queries; Alumbra never offers a rendition the device cannot decode.

## Text tracks

| Format | All platforms |
|---|---|
| WebVTT (sidecar, HLS, MP4) | [● M3](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) |
| SubRip (.srt) | [● M3](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) |
| ASS / SSA | [◐ M3, supported subset (see note)](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) |
| TTML / IMSC1 | [● M3](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) |
| CEA-608 / 708 (in-band) | [● M3](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md) |

ASS / SSA is rendered by Alumbra's own C++ subset renderer on every platform, with no libass or LGPL dependency (ADR-0035). Coverage is a defined supported subset, and fidelity for karaoke, transforms and drawings is weaker than libass.

## Ads

| Capability | Desktop | Web | Android / Google TV | iOS / tvOS |
|---|---|---|---|---|
| Client-side VAST 4 / VMAP | [● M5 (built-in)](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0008-ads-architecture.md) | [● M5 (IMA)](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0008-ads-architecture.md) | [● M6 (IMA)](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0008-ads-architecture.md) | [● M7 (IMA)](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0008-ads-architecture.md) |
| DAI / SSAI | — | [● M10](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0008-ads-architecture.md) | [● M10](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0008-ads-architecture.md) | [● M10](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0008-ads-architecture.md) |
| SGAI (HLS interstitials) | [◐ M10 (built-in)](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0008-ads-architecture.md) | [● M10](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0008-ads-architecture.md) | [● M10](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0008-ads-architecture.md) | [● M10](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0008-ads-architecture.md) |
| Styled fullscreen during ads | [● M5](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0008-ads-architecture.md) | [◐ M5 ⁴](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0008-ads-architecture.md) | [● M6](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0008-ads-architecture.md) | [● M7](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0008-ads-architecture.md) |
| OMID friendly obstructions | — | n/a ⁵ | [● M6](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0008-ads-architecture.md) | [● M7](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0008-ads-architecture.md) |

⁴ On iPhone Safari, only `<video>` can enter native fullscreen and HTML overlays are dropped; Alumbra uses CSS pseudo-fullscreen there (D-24, research/ios-fullscreen-and-ima.md). Google also documents that skippable ads and fullscreen do not work together on iPhone.
⁵ The IMA HTML5 SDK exposes no friendly-obstruction API; instead, Alumbra draws no UI over the ad container while an ad plays (D-23).

## DRM

| Key system | Desktop | Web | Android / Google TV | iOS / iPadOS / tvOS |
|---|---|---|---|---|
| ClearKey (CENC cenc/cbcs) | [● M9](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0005-drm-scope.md) | [● M9 (EME)](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0005-drm-scope.md) | [● M9](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0005-drm-scope.md) | [◐ M9](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0005-drm-scope.md) |
| Widevine | — | [● M9 (EME)](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0005-drm-scope.md) | [● M9 (MediaDrm)](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0005-drm-scope.md) | — |
| FairPlay | — | [● M9 (Safari EME)](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0005-drm-scope.md) | — | [● M9](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0005-drm-scope.md) |
| PlayReady | — | [◐ M9 (Edge EME)](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0005-drm-scope.md) | [◐ device-dependent](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0005-drm-scope.md) | — |

## Evidence tiers

- **Tier 1:** Verified in CI on every change, or on a named device on a fixed cadence where CI cannot run it.
- **Tier 2:** Verified on a reference device before each release.
- **Tier 3:** Best effort. It builds or loads, but no promise, and a defect there does not block a release.
