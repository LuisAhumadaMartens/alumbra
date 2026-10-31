# Compatibility matrix (target)

> This is the **target** matrix: what Alumbra intends to support, and through which mechanism. Each cell
> becomes "verified" only when an automated test proves it. Minimum OS and browser versions are decided in
> definition D-22.

Legend: **●** planned · **◐** partial / conditional (see note) · **—** not possible on this platform ·
**M#** milestone that delivers it.

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
| ASS / SSA (libass) | ● M3 |
| TTML / IMSC1 | ● M3 |
| CEA-608 / 708 (in-band) | ● M3 |

## Ads

| Capability | Desktop | Web | Android / Google TV | iOS / tvOS |
|------------|:-------:|:---:|:-------------------:|:----------:|
| Client-side VAST 4 / VMAP | ● M5 (built-in) | ● M5 (IMA) | ● M6 (IMA) | ● M7 (IMA) |
| DAI / SSAI | — | ● M10 | ● M10 | ● M10 |
| SGAI (HLS interstitials) | ◐ M10 (built-in) | ● M10 | ● M10 | ● M10 |
| Styled fullscreen during ads | ● M5 | ◐ M5 ⁴ | ● M6 | ● M7 |
| OMID friendly obstructions | — | ● M5 | ● M6 | ● M7 |

⁴ On iPhone Safari, only `<video>` can enter native fullscreen and HTML overlays are dropped; Alumbra uses CSS
pseudo-fullscreen there (D-24, [research](research/ios-fullscreen-and-ima.md)).

## DRM

| Key system | Desktop | Web | Android / Google TV | iOS / iPadOS / tvOS |
|------------|:-------:|:---:|:-------------------:|:-------------------:|
| ClearKey (CENC `cenc`/`cbcs`) | ● M9 | ● M9 (EME) | ● M9 | ◐ M9 |
| Widevine | — | ● M9 (EME) | ● M9 (MediaDrm) | — |
| FairPlay | — | ● M9 (Safari EME) | — | ● M9 |
| PlayReady | — | ◐ M9 (Edge EME) | ◐ device-dependent | — |
