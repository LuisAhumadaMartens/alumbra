# Glossary

Terms are grouped by topic. When a term is ambiguous in the industry, the entry states which meaning Alumbra
uses.

## Alumbra layers

| Term | Meaning |
|------|---------|
| **Umbra** | The innermost layer: the platform-agnostic C++20 engine. It must not include any OS or platform header. |
| **Penumbra** | The porting layer between Umbra and a platform: decoders, renderers, audio sinks, networking, DRM, ad SDK bridges. Alumbra's equivalent of Cobalt's Starboard. |
| **Antumbra** | The outermost, visible layer: SDK bindings (TypeScript, Swift, Kotlin, C++), UI renderers, demo apps and design tokens. |
| **Port** | One implementation of the Penumbra contract for a platform (e.g. `penumbra/android`). |

## Project process

| Term | Meaning |
|------|---------|
| **Definition** | An open design question, tracked as an issue labelled `type:definition`, debated until a decision is reached. |
| **ADR** | Architecture Decision Record: the settled outcome of a definition, stored in [`docs/adr/`](adr/). |
| **Epic** | A milestone-scoped capability, tracked as an issue labelled `type:epic`, whose work is split into sub-issues. |

## Media fundamentals

| Term | Meaning |
|------|---------|
| **Container** | File/stream format that carries encoded media: MP4/ISOBMFF, fragmented MP4 (fMP4), MPEG-2 TS, WebM/Matroska. |
| **Codec** | Compression format for audio or video samples: H.264/AVC, H.265/HEVC, VP9, AV1, AAC, Opus, AC-3/E-AC-3, FLAC, MP3. |
| **Demuxer** | Component that reads a container and outputs elementary-stream samples with timestamps. |
| **Sample / access unit** | One encoded frame of audio or video plus its timing (PTS/DTS) and metadata. |
| **PTS / DTS** | Presentation / decode timestamps. They differ when video uses B-frames. |
| **A/V sync** | Keeping video presentation aligned with the audio clock (or another master clock). |
| **TTFF** | Time to first frame: from `load()` (or `play()`) to the first presented video frame. |
| **Rebuffer** | A stall during playback caused by an empty buffer. |
| **HDR10 / HLG** | High-dynamic-range signalling formats (static metadata PQ, and hybrid log-gamma). |

## Streaming

| Term | Meaning |
|------|---------|
| **Progressive** | Playing a single file over `file://` or HTTP (range requests), without a manifest. |
| **HLS** | HTTP Live Streaming: `.m3u8` playlists (multivariant + media playlists) referencing TS or fMP4/CMAF segments. |
| **DASH** | Dynamic Adaptive Streaming over HTTP: an XML MPD describing Periods, AdaptationSets and Representations. |
| **CMAF** | Common Media Application Format: constrained fMP4 segments usable by both HLS and DASH. |
| **ABR** | Adaptive bitrate: choosing which rendition to download based on bandwidth, buffer and device capability. |
| **Rendition / Representation / Variant** | One encoded version of a track at a given bitrate/resolution (HLS uses *variant*/*rendition*, DASH uses *representation*). |
| **LL-HLS** | Low-Latency HLS: partial segments, preload hints, blocking playlist reload. |
| **LL-DASH** | Low-latency DASH: chunked CMAF delivered with HTTP chunked transfer encoding, with latency targets. |
| **Live edge / DVR window** | The newest playable point of a live stream / the seekable range behind it. |

## The "SRT" collision

| Term | Meaning |
|------|---------|
| **SubRip (`.srt`)** | A plain-text **subtitle** format. In Alumbra code and docs it is always called **SubRip**. |
| **SRT protocol** | **Secure Reliable Transport**, a UDP-based low-latency **transport** protocol (`srt://` URLs). In Alumbra it is always called the **SRT protocol**. |

## Text tracks

| Term | Meaning |
|------|---------|
| **WebVTT** | The W3C text track format used by HTML5 and HLS. |
| **ASS/SSA** | Advanced SubStation Alpha: richly styled subtitles (fonts, positioning, karaoke), rendered with libass. |
| **TTML / IMSC1** | XML timed-text formats used mainly with DASH. |
| **CEA-608/708** | Closed captions embedded in the video stream (H.264/HEVC SEI messages). |
| **Cue** | One timed piece of text with its styling and position. |

## Web media APIs

| Term | Meaning |
|------|---------|
| **HTMLMediaElement** | The DOM interface behind `<video>`/`<audio>`. Alumbra's public API mirrors its shape on every platform. |
| **MSE** | Media Source Extensions: lets script append media segments to a `<video>` element. |
| **EME** | Encrypted Media Extensions: the browser API for DRM key systems and sessions. |
| **WebCodecs** | Browser API exposing audio/video encoders and decoders directly to script/WASM. |
| **WASM** | WebAssembly: the compilation target for Umbra in browsers (via Emscripten). |
| **COOP/COEP** | Cross-origin opener/embedder policies; required for WASM threads (`SharedArrayBuffer`). |

## DRM

| Term | Meaning |
|------|---------|
| **CDM** | Content Decryption Module: the (usually closed) component that holds keys and decrypts. |
| **CENC** | Common Encryption (ISO/IEC 23001-7); schemes `cenc` (AES-CTR) and `cbcs` (AES-CBC pattern). |
| **ClearKey** | A non-protected key system defined by EME, useful for testing the full DRM path. |
| **Widevine / FairPlay / PlayReady** | Google / Apple / Microsoft DRM systems, reachable only through platform APIs. |

## Advertising

| Term | Meaning |
|------|---------|
| **IMA** | Google Interactive Media Ads SDKs (HTML5, Android, iOS, tvOS, …). There is no C++ IMA SDK. |
| **VAST / VMAP** | IAB XML standards describing ads (VAST) and ad break schedules (VMAP). |
| **CSAI** | Client-side ad insertion: the player pauses content and plays separately requested ads. |
| **SSAI / DAI** | Server-side ad insertion; Google's implementation is Dynamic Ad Insertion. Ads are stitched into the stream. |
| **SGAI** | Server-guided ad insertion: the server signals breaks in the manifest; the client fetches and plays ad media (e.g. HLS interstitials). |
| **HLS interstitials** | `EXT-X-DATERANGE` entries with `CLASS="com.apple.hls.interstitial"` that schedule separate assets in an HLS timeline. |
| **SCTE-35** | Standard for signalling ad/splice points in a stream. |
| **Ad pod** | A group of ads played back to back in one break. |
| **OMID / Open Measurement** | IAB standard for ad viewability measurement. UI on top of an ad must be registered as a *friendly obstruction*. |
| **Pseudo-fullscreen** | Making the player fill the viewport with CSS instead of the Fullscreen API (needed on iPhone Safari to keep overlays). |

## Porting-layer prior art

| Term | Meaning |
|------|---------|
| **Cobalt** | YouTube's lightweight HTML5 runtime for living-room devices (built on Chromium since Cobalt 26 EAP; Cobalt 27 LTS is the production line). |
| **Starboard** | Cobalt's porting layer; Alumbra's Penumbra plays the same role. |
| **SbPlayer** | Starboard's player interface: the app pushes encoded samples, the platform decodes and renders. |
| **YTLR** | YouTube for the Living Room: the TV/console YouTube experience that runs on Cobalt. |
