# 0044 — Use SurfaceView, Java MediaCodec and AudioTrack on Android, with opt-in tunnelling

- **Status:** Accepted
- **Date:** 2026-11-01
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #21

## Context

On Android, Umbra keeps demuxing, ABR and buffering, and Penumbra feeds compressed samples to `MediaCodec` and owns
presentation and the audio sink. D-21 decides the surface type, the `MediaCodec` mode, frame release, the audio
output and tunnelling. It sets frame pacing and A/V sync on the platform that most often gets them wrong. The
maintainer's standing priority is playback that feels smooth, including at refresh rates above 60 Hz.

Facts the decision rests on, from the definition issue and Android's documentation:

- **`SurfaceView`** hands buffers to SurfaceFlinger as a layer, on a hardware overlay where possible. It uses the
  least power, is the only option for secure (Widevine L1) output, passes HDR through correctly and keeps frame
  timing accurate. **`TextureView`** composites through the app's GPU pipeline, costs power and latency and cannot
  show secure content. Media3 recommends `SurfaceView`.
- **`MediaCodec`** runs in synchronous mode or in asynchronous mode, through Java `setCallback` (API 21) or NDK
  `AMediaCodec_setAsyncNotifyCallback` (API 28). Java from C++ costs a JNI crossing per buffer.
- **Frame release.** `releaseOutputBuffer(index, renderTimestampNs)` presents a frame at the vsync nearest the
  timestamp. Smooth output needs vsync prediction through `AChoreographer`, snapped release times and buffers
  released slightly early, as Media3's `VideoFrameReleaseHelper` does. `Surface.setFrameRate` (API 30) lets the
  display match the content rate.
- **Audio.** `AudioTrack` offers `getTimestamp`, encoded passthrough (AC-3 and E-AC-3 over IEC 61937) and hardware
  A/V sync. AAudio (API 26) is the low-latency PCM API, with no encoded passthrough and no hardware A/V sync.
- **Tunnelling** composes video against the audio clock in hardware. It helps 4K and high-frame-rate output on TV
  SoCs, but is vendor-specific, limits trick play and rate changes, needs tunnel peek (API 31) to show the first
  frame after a seek, and is hard to debug.

Settled elsewhere and not reopened here:

- **Clock** ([ADR-0024](0024-media-clock-and-av-sync.md)). Audio is the master. Tunnelled output synchronises in
  the platform and reports media time. ADR-0024 names D-21 as the owner of Android refresh-rate requests.
- **Frame-rate matching** ([ADR-0030](0030-desktop-graphics-audio-and-ui-renderer.md)). Matching content multiples
  is on by default where seamless, with `playback.display.rate_match` to change it.
- **Decoder fallback** ([ADR-0029](0029-decoder-selection-and-fallback.md)). A secure stream never falls back to a
  non-secure decoder, and passthrough falls back to PCM decode.
- **Floors** ([ADR-0039](0039-compatibility-matrix-and-support-tiers.md)). Android minSdk is 28 (Android 9), and raising it to 30 (Android 11) is pre-approved if
  supporting 28 and 29 becomes a burden during M6. D-22 also answered the minSdk question in D-21.

## Decision drivers

- Smooth presentation at high refresh rates, with a release time that lands on the intended vsync.
- A/V sync anchored on accurate audio timestamps.
- Secure and HDR output work by default.
- Tunnelling's benefits on TV SoCs, without its risks on devices that are not proven.
- Fewest moving parts for M6, with room to move code to the NDK when measurement says so.

## Considered options

1. **A — Conventional pipeline.** `SurfaceView`, Java `MediaCodec` through JNI, app-timed release, `AudioTrack`,
   no tunnelling.
2. **B — A plus opt-in tunnelled mode.** The same, with a second path that uses tunnelling when the decoder, the
   stream and the playback rate allow it.
3. **C — NDK-first.** `AMediaCodec` async and AAudio, with no Java on the media path. Needs API 28, gives up
   passthrough and tunnelling.

The codec API, a sub-question of B:

1. **A — Java async through JNI,** with an M6 spike that measures JNI cost.
2. **B — NDK `AMediaCodec` async,** with Java still needed for capability queries and quirks.
3. **C — Hybrid from the start,** Java for capability queries and NDK for the decode loop.

## Decision

Chosen option: **B, the conventional pipeline plus opt-in tunnelled mode**, with **Java async `MediaCodec`
through JNI** and an M6 spike before any move to the NDK. Even at minSdk 28 the NDK async mode does not remove
Java from the path, since capability queries (`CodecCapabilities`, which the NDK lacks; unverified) and device
quirk handling stay in Java. Media3 has years of those quirks in Java, so starting there is the quickest route.

### Surface

`SurfaceView` by default. `TextureView` only on explicit opt-in and never for protected content, through
`playback.video.surface_type` (`surface_view` or `texture_view`). A `Surface` reaches C++ through
`ANativeWindow_fromSurface`. On surface loss the codec is kept and the output surface is swapped, not recreated;
this is supported from API 23 and is unverified for every vendor, so M6 tests it on devices.

### Codec API and the M6 spike

- Java `MediaCodec` in asynchronous mode through a thin JNI bridge, batching crossings where possible.
- An M6 spike measures JNI cost at 4K60. The hot loop moves to the NDK only if the measurement shows a need.
- The Kotlin and C++ boundary follows [ADR-0038](0038-sdk-bindings-through-a-public-c-abi.md)'s minimal handwritten JNI layer, without fbjni.

### Frame release

- Umbra estimates the content frame rate and selects frames with ADR-0024's cadence estimator.
- Penumbra snaps the release time to vsync with `AChoreographer` and releases about one vsync ahead. That lead is
  unverified and is tuned on devices.
- `Surface.setFrameRate` runs on API 30 and later, with seamless-only changes by default and content-multiple
  matching under ADR-0030's `playback.display.rate_match`. On API 28 and 29 no request is made.

### Audio

`AudioTrack` is the only audio sink in M6. Its timestamps anchor the clock (D-02, ADR-0024), and passthrough and
tunnelling need it. AAudio stays unused until a low-latency use case appears. Passthrough in M6 covers AC-3 and
E-AC-3, chosen per output device, with a fallback to PCM decode (ADR-0029).

### Tunnelling

- `playback.video.tunneling` (`off`, `auto` or `on`) defaults to `auto` only on TV-class devices that pass the
  port conformance kit (D-31, #31), and to `off` elsewhere.
- A playback rate other than 1x leaves tunnel mode. The tunnelled state shows in the stats overlay (D-30, #30).

### HDR

`Display.getHdrCapabilities` joins decoder capability as an ABR filter (D-11, #11).

### Roadmap wording

`docs/roadmap.md` listed AAudio for M6. It now says AudioTrack, matching this decision.

## Consequences

**Positive**
- Secure and HDR output work by default, and frame timing is controlled in C++ with Media3's proven approach.
- One audio sink supports clock anchoring, passthrough and tunnelling.
- Tunnelling is available where it helps and off where it is risky.

**Negative / costs**
- A JNI crossing per buffer, until the spike shows whether it matters.
- Device quirks follow Media3's, and tunnelled mode is opaque to debug.
- Two video paths (app-timed and tunnelled) to test.
- The release lead and vendor behaviour need tuning on real devices.

**Follow-ups**
- #185 builds `MediaCodec` video decoding and `Surface` rendering, including the JNI spike, the release lead and
  the surface-swap behaviour.
- #186 builds the audio sink. Its title still says AAudio, and it is renamed to `AudioTrack`.
- #187 builds tunnelled playback on Google TV with the conformance-kit gate and the stats entry.
- #192 builds the Compose renderer, which hosts the `SurfaceView`.
- A new M6 task measures JNI cost at 4K60 and records whether the decode loop moves to the NDK. If it does, a
  follow-up ADR records it.
- Settings added to the schema under ADR-0017 naming: `playback.video.surface_type` and `playback.video.tunneling`.
  The issue's `android.video.surface` and `android.video.tunneling` are renamed because ADR-0017 keeps platform
  names out of keys; platform availability is metadata.
- If minSdk 28 proves painful during M6, raising it to 30 is pre-approved and follows D-22's process for moving a floor.

## Pros and cons of the options

### A — Conventional pipeline
- Good, because it is Media3's proven model with full frame-timing control.
- Bad, because it has no tunnelling and a JNI crossing per buffer.

### B — A plus opt-in tunnelling
- Good, because TV SoCs get smoother 4K and high-frame-rate output at lower power.
- Bad, because tunnelling is vendor-specific and opaque, and adds a second path to test.

### C — NDK-first
- Good, because the media path is all C++.
- Bad, because it needs API 28, loses passthrough and tunnelling, and trails the Java API.

## References

- [Android: MediaCodec](https://developer.android.com/reference/android/media/MediaCodec)
- [Android NDK: Media (AMediaCodec)](https://developer.android.com/ndk/reference/group/media)
- [AOSP: Multimedia tunneling](https://source.android.com/docs/devices/tv/multimedia-tunneling)
- [Media3: Choosing a surface type](https://developer.android.com/media/media3/ui/surface)
- [Android: AudioTrack](https://developer.android.com/reference/android/media/AudioTrack)
- [Android NDK: AAudio](https://developer.android.com/ndk/guides/audio/aaudio/aaudio)
- [Android NDK: Choreographer](https://developer.android.com/ndk/reference/group/choreographer)
- [Android: Surface](https://developer.android.com/reference/android/view/Surface)
- [Media3: VideoFrameReleaseHelper.java](https://github.com/androidx/media/blob/release/libraries/exoplayer/src/main/java/androidx/media3/exoplayer/video/VideoFrameReleaseHelper.java)
- [Cobalt: AudioTrackBridge.java](https://github.com/youtube/cobalt/blob/main/cobalt/android/apk/app/src/main/java/dev/cobalt/media/AudioTrackBridge.java)
- [ADR-0024](0024-media-clock-and-av-sync.md), [ADR-0029](0029-decoder-selection-and-fallback.md),
  [ADR-0030](0030-desktop-graphics-audio-and-ui-renderer.md)
