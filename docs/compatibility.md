# Compatibility matrix (target)

> This is the **target** matrix: what Alumbra intends to support, and through which mechanism. Each cell
> becomes "verified" only when an automated test proves it. Minimum OS and browser versions are decided in
> [ADR-0039](adr/0039-compatibility-matrix-and-support-tiers.md) and summarised below.
>
> **Web means every device with a modern browser:** desktop, phone, tablet and TV. The **Web** column applies to
> all of them unless a note says otherwise, for example iPhone Safari's fullscreen limits. Which mobile browsers
> and TV web runtimes are tested is set out under Support floors.

Legend: **●** planned · **◐** partial / conditional (see note) · **—** not possible on this platform ·
**M#** milestone that delivers it.

## Support floors

Decided in [ADR-0039](adr/0039-compatibility-matrix-and-support-tiers.md). Tier 1 is verified in CI or on a named
device on every change, tier 2 on a reference device before each release, and tier 3 is best effort. Floors move
only in a minor release.

| Platform | Floor | Tier |
|----------|-------|:----:|
| Web, desktop | Chromium browsers and Firefox, current and previous (plus Firefox ESR); Safari 17.1 | 1 |
| Web, iPhone and iPad | Safari 17.1 (Safari 16.4 to 17.0 unsupported) | 1 |
| Web, Android | Chrome current; Samsung Internet and Firefox | 1; 3 |
| Web, TV | Google TV and Android TV WebView (tier 2); Tizen 8 and webOS 24 | 2; 3 |
| macOS | 14 | 1 |
| Linux | Ubuntu 26.04 LTS (x86-64, arm64) and current Fedora; Wayland and X11 | 1 |
| Windows | Windows 11 x64: engine builds, not supported ([ADR-0031](adr/0031-windows-support-scope-and-toolchain.md)) | n/a |
| iOS, iPadOS, tvOS | 17 | set in M7 |
| Android / Google TV | `minSdk` 28 (Android 9); a move to 30 is pre-approved if 28 becomes a burden in M6 | set in M6 |

Web builds need WASM SIMD. Threads are optional ([ADR-0037](adr/0037-wasm-threading-and-cross-origin-isolation.md)).
Toolchain floors are in [ADR-0013](adr/0013-cpp-language-standard.md).

## Sources and protocols

| Source | Desktop | Web | Android / Google TV | iOS / iPadOS / tvOS |
|--------|:-------:|:---:|:-------------------:|:-------------------:|
| Progressive MP4 / fMP4 | ● M1 | ● M4 | ● M6 | ● M7 |
| Progressive MPEG-TS | ● M1 | ● M4 | ● M6 | ● M7 |
| Progressive WebM / MKV | ● M1 | ◐ M4 ¹ | ● M6 | ● M7 |
| HLS (TS, fMP4/CMAF) | ● M2 | ● M4 | ● M6 | ● M7 |
| DASH | ● M2 | ● M4 | ● M6 | ● M7 |
| LL-HLS | ● M8 | ● M8 | ● M8 | ● M8 |
| LL-DASH | ● M8 | ● M8 | ● M8 | ● M8 |
| `rtmp://`, `rtsp://`, `srt://`, `udp://` | ● M8 | — ² | ● M8 | ◐ M8 ³ |

¹ Via the WebCodecs fallback when MSE cannot express the stream. ² Browsers expose no raw sockets; WebRTC/WHEP is
evaluated in D-14. ³ Subject to App Store networking and licensing constraints (D-32).

## Codecs

| Codec | Desktop (FFmpeg/dav1d) | Web (MSE/WebCodecs) | Android (MediaCodec) | Apple (VideoToolbox/AudioToolbox) |
|-------|:-------:|:---:|:-------:|:-----:|
| H.264 / AVC | ● M1 | ● M4 | ● M6 | ● M7 |
| H.265 / HEVC | ● M1 | ◐ browser-dependent | ◐ device-dependent | ● M7 |
| VP9 | ● M1 | ◐ browser-dependent | ◐ device-dependent | ◐ device-dependent |
| AV1 | ● M1 | ◐ browser-dependent | ◐ device-dependent | ◐ device-dependent |
| AAC | ● M1 | ● M4 | ● M6 | ● M7 |
| Opus | ● M1 | ● M4 | ● M6 | ◐ |
| AC-3 / E-AC-3 | ● M1 | ◐ | ◐ passthrough/decoder | ● M7 |
| FLAC, MP3 | ● M1 | ● M4 | ● M6 | ● M7 |
| HDR10 / HLG | ◐ M11 | ◐ M11 | ◐ M11 | ◐ M11 |

"Device-/browser-dependent" cells are resolved at runtime by capability queries; Alumbra never offers a rendition
the device cannot decode.

## Text tracks

| Format | All platforms |
|--------|:-------------:|
| WebVTT (sidecar, HLS, MP4) | ● M3 |
| SubRip (`.srt`) | ● M3 |
| ASS / SSA | ◐ M3, supported subset (see note) |
| TTML / IMSC1 | ● M3 |
| CEA-608 / 708 (in-band) | ● M3 |

ASS / SSA is rendered by Alumbra's own C++ subset renderer on every platform, with no libass and no LGPL
dependency ([ADR-0035](adr/0035-text-cue-model-styling-and-ass-renderer.md)). Coverage is a defined supported
subset, and fidelity for karaoke, transforms and drawings is weaker than libass.

## Ads

| Capability | Desktop | Web | Android / Google TV | iOS / tvOS |
|------------|:-------:|:---:|:-------------------:|:----------:|
| Client-side VAST 4 / VMAP | ● M5 (built-in) | ● M5 (IMA) | ● M6 (IMA) | ● M7 (IMA) |
| DAI / SSAI | — | ● M10 | ● M10 | ● M10 |
| SGAI (HLS interstitials) | ◐ M10 (built-in) | ● M10 | ● M10 | ● M10 |
| Styled fullscreen during ads | ● M5 | ◐ M5 ⁴ | ● M6 | ● M7 |
| OMID friendly obstructions | — | n/a ⁵ | ● M6 | ● M7 |

⁴ On iPhone Safari, only `<video>` can enter native fullscreen and HTML overlays are dropped; Alumbra uses CSS
pseudo-fullscreen there (D-24, [research](research/ios-fullscreen-and-ima.md)). Google also documents that skippable
ads and fullscreen do not work together on iPhone.

⁵ The IMA HTML5 SDK exposes no friendly-obstruction API; instead, Alumbra draws no UI over the ad container while an
ad plays (D-23).

## DRM

| Key system | Desktop | Web | Android / Google TV | iOS / iPadOS / tvOS |
|------------|:-------:|:---:|:-------------------:|:-------------------:|
| ClearKey (CENC `cenc`/`cbcs`) | ● M9 | ● M9 (EME) | ● M9 | ◐ M9 |
| Widevine | — | ● M9 (EME) | ● M9 (MediaDrm) | — |
| FairPlay | — | ● M9 (Safari EME) | — | ● M9 |
| PlayReady | — | ◐ M9 (Edge EME) | ◐ device-dependent | — |
