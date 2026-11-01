# 0016 — Define the port contract as a push-model player with a shared reference implementation

- **Status:** Accepted
- **Date:** 2026-10-31
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #4

## Context

Penumbra is Alumbra's Starboard: the contract each platform port implements to turn encoded media into picture
and sound. Once four ports implement it, changing it means changing all of them.

The platforms pull in different directions. Media Source Extensions accept container bytes and decode, render
and synchronise inside the browser. WebCodecs and the desktop (FFmpeg, dav1d, SDL3) offer granular decoders and
sinks. Android's MediaCodec is granular, but tunneled playback and secure decoders render straight to a surface,
so frames are never visible to the engine. Apple's `AVSampleBuffer*` classes take compressed samples and
synchronise themselves, while `AVPlayer` only accepts a URL.

Starboard resolved the same tension with a push-model C API (`SbPlayer`) and a shared filter-based player that
most ports reuse. Chromium's `media::Renderer` has the same shape: `RendererImpl` over decoders and sinks next
to platform renderers such as `MediaFoundationRenderer`.

## Decision drivers

- Every target platform, including MSE, tunneled and hardware-secure output, must be expressible.
- Identical sync, frame-drop and statistics behaviour wherever the platform allows the engine to own it.
- Bounded memory on 1 GB-class devices.
- Low effort for a new port, and headless testing through a fake port.
- A contract that can still evolve quickly while it is young.

## Considered options

1. **A — Push-model player:** the port accepts encoded samples and owns decode, render and sync.
2. **B — Granular components:** the port supplies decoders and sinks; Umbra owns sync.
3. **C — URL player:** the port receives a URL and plays it.
4. **D — Layered:** A is the contract; a shared reference implementation provides A from B-style components.

## Decision

Chosen option: **D**, because it is the only option that serves MSE, secure and tunneled output and Apple's
sample renderers while keeping identical engine-owned behaviour wherever a port can expose components. B cannot
express platforms that never hand frames back, and C gives up the engine's control of ABR, text, ads and errors.

### Interface

The contract is a set of **C++ abstract interfaces** for v1. All ports build in-tree with one toolchain, so a
stable binary boundary buys nothing yet, and C++ keeps a young contract cheap to change. A C ABI may be added
later as a thin layer if out-of-tree ports or an independently updatable core become goals.

### The player contract (A)

- **Streams.** A player is created with its stream configurations (codec, codec configuration, encryption) and
  an output mode per video stream.
- **Samples.** Umbra writes demuxed access units: data, presentation and decode timestamps, duration, key-frame
  and discontinuity flags, configuration changes, and encryption metadata (scheme `cenc` or `cbcs`, key ID, IV,
  subsample map, pattern) from the first version, so adding DRM does not change the sample type.
- **Fragment pass-through.** A port may declare that it accepts original fMP4/CMAF fragments. Umbra then passes
  fragments that need no transformation straight through, which spares the MSE port a remux. Umbra still parses
  them for timing and ABR, and everything else arrives as samples.
- **Flow control is pull.** The port signals "needs data" per stream. Umbra makes at most one write per signal,
  of up to the port-declared maximum number of samples per write, and signals end of stream per stream. Pacing
  and memory belong to the port, which knows its decoder queue depth or MSE quota.
- **Seeks carry a ticket.** Callbacks and samples with a stale ticket are discarded.
- **Reporting.** Umbra queries media time from the port (the media clock is D-02's) and reads frame counters
  (decoded, dropped, corrupted). Errors arrive as callbacks.
- **Threads.** Port callbacks are posted to Umbra's control sequence, and a port makes no callbacks after its stop
  completes ([ADR-0015](0015-threading-and-task-runners.md)).

### The reference player (B inside D)

Penumbra ships a reference player that implements the contract from components: video and audio decoders, a
video sink fed by a lock-free frame queue, and an audio sink fed by real-time rings, with sync, frame dropping and
statistics owned by shared code (D-02). The desktop and WebCodecs ports supply components only. MSE, Apple
`AVSampleBuffer*` and tunneled or secure Android output implement the contract directly.

### URL mode

A port may offer an optional URL-player mode where a native URL player is the best path, such as `AVPlayer` for
FairPlay with HLS, HLS Interstitials and AirPlay. It runs with reduced engine features (the native player owns
ABR, and text and ads may be limited), and each reduced feature is documented. When a port uses it is D-20's
decision.

### Slots reserved now, detailed elsewhere

- **Output and overlays.** A port declares a punch-out or texture output mode per video stream; punch-out ports
  receive a bounds and z-order call. Composition of UI and ad overlays is decided by D-19 (#19) and D-23 (#23).
- **Capabilities.** One query, shaped like the web's `MediaCapabilities.decodingInfo`, answers supported, smooth
  and power-efficient for a codec, profile, level, size, frame rate, HDR format and key system. D-06 (#6) builds
  the decoder fallback chain on it.
- **DRM.** The sample's encryption metadata is fixed above; where the CDM attaches is decided by D-27 (#27).
- **Concurrent players.** A port declares how many players it supports (at least one). Failing to create one is
  an expected, reportable error, which ad preloading (D-25, #25) and picture-in-picture must handle.
- **Conformance.** A suite in the spirit of Starboard's NPLB runs against every port and against a fake port that
  Umbra's own tests use. Its M1 scope is lifecycle, seek tickets, flow control, end of stream and time reporting;
  the wider testing strategy is D-31 (#31).

## Consequences

**Positive**
- Every target platform has a defined path, including MSE, tunneled and secure output and Apple's URL player.
- Sync, dropping and statistics behave identically on every port that uses the reference player.
- Pull flow control bounds memory per port, and seek tickets remove a class of stale-callback bugs.
- Umbra tests run against a fake port without platform code.

**Negative / costs**
- Two documented ways to write a port, and one more layer on the reference path.
- The reference player is a substantial component of its own.
- Ports that implement the contract directly own their sync and statistics, so their behaviour must be held to
  the conformance suite.
- C++ interfaces give no binary stability; the core and its ports ship together.

**Follow-ups**
- #51 defines the shared media types (time, rationals, samples with encryption metadata, stream information).
  Types used by the port contract live in the Penumbra interface, which may not include Umbra headers.
- #67 writes the Penumbra interface headers v1.
- D-02 (#2), D-06 (#6), D-19 (#19), D-20 (#20), D-23 (#23), D-25 (#25), D-27 (#27) and D-31 (#31) fill in the
  reserved slots.

## References

- [D-04 discussion](https://github.com/LuisAhumadaMartens/alumbra/issues/4)
- [Alumbra research: Cobalt and Starboard](../research/cobalt-and-starboard.md)
- [Cobalt: starboard/player.h](https://github.com/youtube/cobalt/blob/main/starboard/player.h)
- [Cobalt: starboard/shared/starboard/player/filter](https://github.com/youtube/cobalt/tree/main/starboard/shared/starboard/player/filter)
- [Cobalt: starboard/nplb](https://github.com/youtube/cobalt/tree/main/starboard/nplb)
- [Cobalt: starboard/tvos/shared/media/url_player.h](https://github.com/youtube/cobalt/blob/main/starboard/tvos/shared/media/url_player.h)
- [Chromium: media/base/renderer.h](https://chromium.googlesource.com/chromium/src/+/main/media/base/renderer.h)
- [Chromium: media/renderers/renderer_impl.h](https://chromium.googlesource.com/chromium/src/+/main/media/renderers/renderer_impl.h)
- [W3C: Media Source Extensions](https://www.w3.org/TR/media-source-2/)
- [W3C: WebCodecs](https://www.w3.org/TR/webcodecs/)
- [W3C: Media Capabilities](https://www.w3.org/TR/media-capabilities/)
- [Android: Multimedia tunneling](https://source.android.com/docs/devices/tv/multimedia-tunneling)
- [Apple: AVSampleBufferDisplayLayer](https://developer.apple.com/documentation/avfoundation/avsamplebufferdisplaylayer)
