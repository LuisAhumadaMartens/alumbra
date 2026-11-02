# 0029 — Select decoders in the reference player through an ordered fallback chain

- **Status:** Accepted
- **Date:** 2026-11-01
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #6

## Context

D-06 decides how Alumbra chooses a decoder for each stream and when it may switch to another one: the order of
hardware and software candidates, what triggers a fallback, how much data is kept to re-feed a new decoder, and how
decoder limits feed ABR. The choice decides how smooth playback is and how much power it uses. Hardware decoders
keep 4K and high frame rates smooth at a fraction of the CPU and power cost. Software decoders are deterministic,
testable and the same on every machine.

Platforms describe their decoders differently (`MediaCodecInfo.VideoCapabilities`, `VTIsHardwareDecodeSupported`,
`MediaCapabilities.decodingInfo()`, `VideoDecoder.isConfigSupported()`), and decoders fail in several ways. A
decoder can refuse its configuration, fail mid-stream, be too slow for the content, or lose a capability when the
output changes. D-06 blocks #84. It also decides whether desktop frames reach the renderer as CPU buffers or GPU
surfaces, which D-19 (#19) has to handle.

Prior art agrees on the basic shape. Media3 orders `MediaCodec` decoders through a `MediaCodecSelector`, hardware
first, and `DefaultRenderersFactory.setEnableDecoderFallback` moves to the next decoder when one fails to
initialise. Chromium's `DecoderSelector` tries decoders in priority order. Its `DecoderStream` keeps the buffers
sent to a new decoder until that decoder produces its first frame, so a decoder that fails early can be replaced
without losing data. mpv leaves hardware decoding off by default. With `--hwdec=auto` it tries only an allowlist of
backends known to work and falls back to software otherwise. Chromium ships its GPU blocklists as reviewed data
files.

Accepted decisions already settle several parts, and this ADR does not reopen them:

- **Capabilities** ([ADR-0016](0016-penumbra-port-contract.md),
  [port contract](../architecture/port-contract.md#player-and-capabilities)). `CapabilityProvider::QueryDecoding`
  takes the MIME type with an RFC 6381 codec string, size, frame rate, bitrate, HDR format, key system and
  encryption scheme. It answers supported, smooth and power-efficient, with the preferred output mode. Profile,
  level and bit depth travel in the codec string, the transfer function in the HDR format and the secure path in
  the key system. This answers the issue's question about one capability structure for all ports.
- **Recovery** ([ADR-0018](0018-error-model-and-recovery.md#recovery-ladder)). Switching the decoder or recreating
  the port player is step 4 of the recovery ladder. Ports report errors with a category and a transient, recreate
  or fatal hint, and the catalogue has `DecoderFallback` (4002, degraded).
- **FFmpeg** ([ADR-0014](0014-dependency-and-licensing-policy.md#ffmpeg)). FFmpeg is built with
  `--disable-autodetect`. Its components, hardware acceleration included, are enabled only through #85's reviewed
  list.
- **Golden output** ([ADR-0021](0021-testing-strategy.md#golden-output-and-actual-av-sync)). Exact frame hashes
  are allowed only for decoders shown to be deterministic. Hardware-decoded output uses calibrated comparisons.
- **Web output** ([ADR-0004](0004-web-output-hybrid-mse-webcodecs.md)). MSE or WebCodecs is chosen per stream at
  load time.

The issue's proposal conflicts with accepted decisions in places, which this ADR resolves:

- It gives Umbra the ordering policy, with ports supplying candidates. ADR-0016 puts decoders inside the Penumbra
  reference player, and Umbra sees only the player contract. The order therefore lives in the reference player
  ([who orders candidates](#who-orders-candidates)), not in Umbra.
- Its learned per-device history would need state the engine does not keep: the app persists preferences and the
  engine only receives values ([ADR-0017](0017-settings-schema-and-configuration.md#context)), so a history would
  need app-provided storage. Remote blocklist updates would be a request Alumbra makes on its own, which
  [ADR-0020](0020-observability-logging-tracing-qoe-metrics.md#qoe-metrics-and-statistics) allows only as an
  explicit integrator opt-in. This ADR rejects both.
- Hardware decoding on desktop needs FFmpeg hwaccels added to #85's reviewed list under ADR-0014. Autodetection
  cannot switch it on.
- CI jobs that compare exact golden hashes must force software decoding (ADR-0021).
- The error catalogue has no code for a changed capability. The port contract assigns that code to a catalogue or
  D-06 follow-up, which this ADR lists among its [consequences](#consequences).

## Decision drivers

- Smoothness, including 4K and high frame rates on 120 Hz and 144 Hz displays.
- Power efficiency: hardware decoding wherever it is reliable, without hidden copies that cancel its savings.
- Robust startup on fragmented hardware, especially Android, without thrashing between decoders.
- One policy on every port built from components, within ADR-0016's contract.
- Bounded memory on 1 GB-class devices (D-12, #12).
- Behaviour that can be reproduced and debugged: no hidden per-device state and no phone-home requests.
- Secure output that never weakens content protection.

## Considered options

The definition has six questions.

Fallback model:

1. **A — Static selection, no fallback.**
2. **B — Ordered chain, early errors:** fall back on a configuration failure or an error before the first output.
3. **C — Ordered chain plus a drop trigger:** B, plus sustained dropped frames first ask ABR to cap the
   resolution and switch decoder only at the lowest rung. A static blocklist applies, with no learned history.
4. **D — Adaptive chain with learning:** a per-device history persisted across sessions reorders the candidates.

Who orders candidates, and whether `QueryDecoding` is the single capability structure:

1. **A — The reference player orders**, filtered by `QueryDecoding` and the blocklist; ports supply candidates.
2. **B — Umbra orders**, through decoder enumeration added to the port contract.
3. **C — Each port orders** its own candidates.

Data kept to re-feed a fallback decoder:

1. **A — Retain until first output**, and reseek for later fallbacks.
2. **B — Always keep the current group of pictures.**
3. **C — No retention:** every fallback reseeks.

Dropped-frame threshold and attribution:

1. **A — Attributed drops:** only decode-late drops count, more than 10 % over 5 s.
2. **B — Any drops, faster:** all drops count, more than 5 % over 2 s.
3. **C — Statistics only in v1.**

Later fallback paths (audio passthrough, Widevine L1 to L3, MSE to WebCodecs):

1. **A — Record the guardrails now.**
2. **B — Defer to their milestones**, recording only that secure streams never fall back.
3. **C — Allow MSE to WebCodecs rebuilds** mid-stream for clear content.

Blocklist format, governance and updates:

1. **A — Repository data plus app overrides.**
2. **B — Repository data only.**
3. **C — A remotely updatable list** from a URL the integrator hosts.

## Decision

Chosen options: **C for the fallback model, and A for the other five questions.** An ordered chain with early
fallback is the proven startup behaviour of Media3 and Chromium. The drop trigger also handles decoders that are too
slow, but it acts through ABR first and counts only drops the decoder caused, so a compositor stall never swaps a
decoder. The reference player owns the order, as ADR-0016 requires, so every port built from components shares one
policy. A static, reviewed blocklist keeps behaviour reproducible, and nothing persists between playbacks.

### The fallback chain

- Each stream, video or audio, has its own ordered list of candidate decoders.
- The reference player creates the first candidate and moves to the next one when:
  - the candidate fails to be created or configured; or
  - the candidate reports a decode error before its first output, meaning its first decoded frame or audio buffer.
- A move before the first output re-feeds the retained samples to the new decoder
  ([re-feed data](#re-feed-data)), so startup loses no data.
- After the first output:
  - A corrupted sample is counted as corrupted and is not a fallback.
  - Any other decoder error is reported to Umbra with a recreate hint and handled at step 4 of ADR-0018's ladder,
    through a reseek to the current position. The reference player recreates the same candidate first, because
    many mid-stream failures, such as a decoder lost while the app was in the background, clear on recreation.
  - A recreated decoder that fails before its own first output moves the chain on, as at startup.
- A configuration change that the decoder cannot apply in place recreates the same candidate. If it then fails
  before its next output, the chain moves on.
- A candidate the chain has left stays excluded for the rest of that playback. The chain only moves forward, so
  it cannot oscillate between decoders. Nothing persists between playbacks.
- When no candidate remains, the stream fails with `CodecUnsupported` (4001) if no candidate supports it, or with
  `DecoderInitializationFailed` (4003) if every candidate failed.
- Every move is reported as `DecoderFallback` (4002, degraded): a warning event for the app and for telemetry,
  with the previous decoder, the new decoder and the reason.
- After a fallback, Umbra queries capabilities again and ABR filters the renditions against the new decoder
  (D-11, #11).
- With `decode.fallback.enabled` set to false, the chain never moves on: the first candidate's failure is reported
  as an error, and the drop trigger may still cap the resolution but never switches decoder. Tests use this to pin
  a decoder.

### Who orders candidates

- The reference player owns the order, the same on every port built from components:
  1. hardware decoders;
  2. platform software decoders, such as Android's `c2.android.*`;
  3. bundled software decoders, such as dav1d and libavcodec.
- Ports supply their candidates and quirks through `ComponentFactory`. Each candidate states its name, its kind
  (one of the three above), whether it is secure, whether it outputs CPU buffers or GPU surfaces, and what it
  supports. Quirks are workarounds for specific decoders and devices, kept in port code, as Media3 keeps them in
  `MediaCodecVideoRenderer`.
- The reference player removes the candidates that do not support the stream, that the blocklist matches or that
  the settings exclude, and orders the rest. Within a kind, the port's order applies; for AV1, dav1d ranks ahead of
  any FFmpeg AV1 path (#86).
- On ports built from components, `QueryDecoding` answers from the same filtered list, so ABR filters renditions
  against the decoder that will actually run. `QueryDecoding` stays the one capability structure.
- The candidate list is an addition to `ComponentFactory`, the reference player's component interface. The player
  contract that Umbra drives does not change. #84 adds the list under the port contract's
  [versioning rule](../architecture/port-contract.md#placement-and-versioning).
- Umbra sees only capabilities, the decoder settings and errors with a recreate hint. Penumbra cannot read
  Umbra's settings registry, so Umbra passes the resolved decoder settings and the integrator's blocklist overrides
  to the port in the player configuration.
- Ports that implement the player directly (MSE, Apple's `AVSampleBuffer*` path, tunnelled and secure Android
  output) keep their platform's internal choice. They still answer `QueryDecoding`, report errors with hints and
  follow the [guardrails](#guardrails-for-later-fallback-paths).
- `decode.prefer_hardware` adjusts the order:
  - `auto` (default): the order above;
  - `never`: software candidates only;
  - `always`: hardware candidates only, so a hardware failure is reported instead of falling back to software.
    This is for testing, and for integrators who would rather fail than drain the battery.

  A secure decoder that the key system requires is never removed by this setting.

### Desktop decoding

- Desktop M1 decodes in software, as the roadmap requires, with libavcodec (#85) and dav1d (#86). Its candidate
  lists have no hardware entries yet. `decode.prefer_hardware` is accepted there and has no effect until hardware
  decoding arrives.
- Hardware decoding on desktop is added right after M1. It follows mpv's `--hwdec=auto` model: only allowlisted,
  known-good backends are tried, starting with those mpv actively supports on these systems (VideoToolbox on macOS,
  VA-API and NVDEC on Linux), and the chain falls back to software when they fail. Each backend joins #85's
  reviewed FFmpeg list under ADR-0014 before it is built, and its libraries and headers are reviewed like any
  dependency.
- Hardware decoding pays off for 4K and high frame rates only when its frames stay on the GPU. Hardware frames are
  therefore imported into the D-19 renderer as GPU surfaces, without a copy. Copying them back to system memory is
  not used by default, because the copy costs much of the CPU time and power that the hardware saved; mpv's manual
  describes the same trade-off for its `-copy` modes.
- **D-19 (#19) must design its video path for GPU-surface frames from the start**, next to M1's CPU-buffer frames,
  so that hardware decoding lands without a renderer redesign. `DecodedVideoFrame` already describes a hardware
  frame as `PixelFormat::kOpaque` with port-owned storage
  ([media types](../architecture/media-types.md#decoded-output)). The renderer must import such a surface and
  apply the same colour conversion as for CPU frames.

### Re-feed data

- From the key frame where a decoder instance starts, the reference player keeps the samples it writes to that
  decoder until the decoder's first output, as Chromium's `DecoderStream` does. The kept samples hold references to
  the samples' shared bytes, so nothing is copied, and they are released at the first output. Their memory counts
  against D-12's budgets.
- A fallback before the first output re-feeds the kept samples to the next candidate.
- A later fallback, after a mid-stream error or from the drop trigger, flushes and re-demuxes from the current
  position under a new seek ticket. Umbra drives that reseek, because Umbra owns demuxing, and the port contract's
  ticket rules already discard stale samples and callbacks. The cost is a short visible hitch and possibly some
  refetched data.

### Dropped frames and the drop trigger

- The reference player attributes every dropped video frame. D-02 (#2, ADR-0024) defines when a frame is dropped:
  when it is more than its lateness threshold late, or when decoding skips to the next keyframe because video fell
  too far behind. Frames that the cadence skips, because the content is faster than the display, are not drops and
  never count. Each drop is one of:
  - **decode-late:** the frame left the decoder after the deadline of the refresh it was due for, or was never
    decoded because of a keyframe skip;
  - **present-late:** the frame was decoded in time but missed its refresh, for example because the renderer or
    the compositor stalled.
- Only decode-late drops count towards the trigger. Present-late drops are reported for the renderer (D-19) and
  never change the decoder or the rendition.
- The trigger fires when decode-late drops exceed `decode.dropped_frames.max_percent` (default 10 %) of the frames
  due within `decode.dropped_frames.window_ms` (default 5000) of playback. The window restarts after a seek, a
  resume, a playback-rate change, a rendition switch and a decoder switch, so start-up work is not counted.
  The threshold is deliberately loose, because the trigger changes playback rather than predicting it.
- When the trigger fires:
  1. Umbra asks ABR to cap the resolution below the current rendition (M2, D-11). After each cap the trigger waits
     for a full new window before it can fire again.
  2. At the lowest rung ABR may choose, the next firing switches to the next candidate in the chain through a
     reseek. A source with a single rendition is already at its lowest rung.
  3. When no candidate remains, playback continues on the current decoder and the condition is reported as
     degraded, not fatal.
- A secure stream switches only to another secure candidate.
- The reference player detects the condition, because it owns the attributed counts, and reports it to Umbra,
  which owns ABR. #84 defines that event and how Umbra requests the switch: in place under a new seek ticket, or
  by recreating the port player with the slow decoder excluded.
- M1 records the attributed counts in the statistics and takes no action. The cap and the switch arrive with ABR
  in M2.
- The defaults are provisional until M11's dropped-frame measurements confirm or replace them.

### Guardrails for later fallback paths

- **Audio passthrough (M6).** When the output sink rejects a passthrough format, for example E-AC-3 over HDMI,
  at configuration or after a route change, the stream falls back to decoding through the chain.
- **Secure decoding (M9, D-27).** A secure stream never falls back to a non-secure decoder. A lower robustness
  level, such as Widevine L3 instead of L1, is used only when the app's key-system configuration requests it and
  the licence allows it. Otherwise a secure decoder failure is a decode or DRM error. D-27 (#27) implements this.
- **Web output (M4, D-16).** MSE or WebCodecs is chosen at load time only and is never rebuilt mid-stream, as
  ADR-0004 decided. The WebCodecs path uses the reference player, so the chain applies inside it. D-16 (#16)
  defines its candidates, such as the browser's decoder with each `hardwareAcceleration` hint and any bundled
  WebAssembly decoder.

### Blocklist

- The blocklist is a reviewed TOML file in the repository, next to the settings schema and the error catalogue.
  Each entry has a stable ID. It matches on port, OS version range, device model or GPU (vendor, device and driver
  version), decoder and codec, and it records the reason with a link to the issue that justifies it.
- The desktop hardware allowlist uses the same file and generator.
- An entry may also switch off a feature that is not a decoder for a matching GPU and driver. Known-bad
  variable-refresh-rate drivers use this for the display rate matching of
  [ADR-0024](0024-media-clock-and-av-sync.md), and [ADR-0030](0030-desktop-graphics-audio-and-ui-renderer.md)
  consumes the entries.
- A standard-library Python generator, like those for settings and errors, validates the file and generates a C++
  table for the reference player and a Markdown reference page. The generated files are committed and checked for
  drift in CI.
- The blocklist ships with each release. Changing it takes a pull request reviewed like code.
- Integrators can exclude further decoders and ignore built-in entries through settings or the API, for example to
  work around a device before Alumbra's next release. They may deliver those values from their own configuration
  service, but Alumbra itself fetches nothing.
- There are no remote updates.

### Testing and observability

- Fake decoders in the reference player's tests cover each trigger: a configuration failure, an error before the
  first output with re-fed samples, a mid-stream error with a reseek, the drop trigger with attributed drops, and
  exclusion for the rest of the playback. #71's conformance kit runs the chain on every port built from
  components.
- CI jobs that compare exact golden hashes set `decode.prefer_hardware` to `never` and `decode.fallback.enabled` to
  false. An unnoticed fallback then fails the test instead of comparing another decoder's output (ADR-0021).
  Hardware paths use calibrated comparisons.
- The selected decoder, its kind and output type, every fallback and the attributed drop counts appear in the
  statistics, the debug overlay and the `decode` log and trace category (ADR-0020). The public dropped-frames
  statistic stays a total; the decode-late and present-late split lives in the debug extension.

### Settings

This ADR records these keys; it does not add them to the schema. All start as `experimental`.

| Key | Type and default | Apply |
|-----|------------------|-------|
| `decode.prefer_hardware` | enum `auto`, `always` or `never`; `auto` | `next_load` |
| `decode.fallback.enabled` | boolean; `true` | `next_load` |
| `decode.dropped_frames.max_percent` | integer percentage; 10 | `live` |
| `decode.dropped_frames.window_ms` | duration; 5000 | `live` |
| `decode.blocklist.extra_decoders` | string list of decoder names, each optionally with a codec; empty | `next_load` |
| `decode.blocklist.ignored_entries` | string list of built-in entry IDs; empty | `next_load` |

#84 already plans `decode.video.preferred_decoders`, which reorders the filtered candidates. It cannot restore a
blocklisted candidate or give a secure stream a non-secure decoder.

## Consequences

**Positive**

- Startup survives decoders that fail to configure or fail early, without losing data and at almost no memory
  cost.
- A slow decoder is handled by lowering the resolution first, and compositor stalls never swap a decoder.
- Every port built from components shares one policy, and the player contract does not change.
- The blocklist is auditable and reproducible, integrators can work around a device at once, and Alumbra makes no
  network requests of its own.
- Desktop hardware decoding can follow M1 without a renderer redesign, bringing smooth 4K and high frame rates at
  lower power.

**Negative / costs**

- Ports that implement the player directly keep their platform's choice, so only the guardrails and the
  conformance kit hold their behaviour.
- A fallback after the first output shows a short hitch and may refetch data.
- Drop attribution needs decoder-output and vsync timing in the reference player, and the thresholds stay
  provisional until M11.
- Built-in blocklist fixes wait for a release unless an integrator overrides them.
- Desktop M1 uses more CPU and power for 4K and high frame rates than hardware decoding would.
- D-19 must support GPU-surface frames before a desktop hardware decoder exists to exercise them.

**Follow-ups**

- **#84** implements the chain, the candidate list in `ComponentFactory`, retention until the first output, the
  reseek request, drop attribution and its statistics, the drop-trigger event, and the chain and drop settings.
- **#85** keeps libavcodec software-only in M1. Hwaccels join its reviewed configuration only through the desktop
  hardware decoding task.
- **#86** ranks dav1d ahead of any FFmpeg AV1 path.
- **Desktop hardware decoding (new task, right after M1):** the allowlist; hwaccels in #85's reviewed list under
  ADR-0014; zero-copy import into the D-19 renderer; `QueryDecoding` answers from `VTIsHardwareDecodeSupported`
  and the VA-API and NVDEC capability queries; and calibrated golden comparisons for the hardware paths.
- **D-19 (#19):** design the video path for both CPU-buffer and GPU-surface (`kOpaque`) frames, and verify
  zero-copy surface import into the renderer before the hardware task starts.
- **D-11 (#11):** consume the cap request, decide how a cap is expressed and lifted, and filter renditions again
  after a fallback.
- **D-16 (#16):** keep the MSE or WebCodecs choice at load time and define the WebCodecs candidates.
- **D-27 (#27):** implement the secure-decoding guardrail and choose robustness from the app's key-system
  configuration.
- **D-12 (#12):** count the retained samples in the memory budgets.
- **Blocklist (new task):** the TOML format, the generator, the drift check, the reference page and the integrator
  overrides.
- **Error catalogue:** add a code for a changed capability, which ports report with the `kCapability` category and
  a recreate hint. The port contract leaves this code to a catalogue or D-06 follow-up.
- **Settings to add**, recorded above and not added by this ADR: `decode.prefer_hardware`,
  `decode.fallback.enabled`, `decode.dropped_frames.max_percent`, `decode.dropped_frames.window_ms`,
  `decode.blocklist.extra_decoders` and `decode.blocklist.ignored_entries`.
- M6 implements the passthrough fallback, and M11 measures and confirms the drop thresholds.

## Pros and cons of the options

### Fallback model

#### A. Static selection, no fallback

- Good, because it is predictable and matches MSE, where the browser hides the choice.
- Bad, because it is fragile on Android hardware and against mid-stream errors.

#### B. Ordered chain, early errors

- Good, because startup survives decoders that fail to configure or fail early, as in Media3 and Chromium.
- Bad, because it does not help when a decoder is too slow for the content.

#### C. Ordered chain plus a drop trigger

- Good, because slow decoders are handled, through ABR first, without thrashing.
- Bad, because it needs drop attribution in the reference player.

#### D. Adaptive chain with learning

- Good, because it is the most robust over time on a given device, as Chromium's decode history is for its
  smoothness answers.
- Bad, because the engine persists no state, so the history needs app storage.
- Bad, because behaviour then depends on hidden history, which makes it harder to reproduce and debug.

### Who orders candidates

#### A. The reference player orders

- Good, because it fits ADR-0016 without changing the player contract, and every port built from components
  shares one policy.
- Bad, because ports that implement the player directly keep their own choice.

#### B. Umbra orders through enumeration

- Good, because the policy lives entirely in the engine.
- Bad, because it changes the player contract and reaches into what ADR-0016 gave to the port.

#### C. Each port orders

- Good, because it uses each platform's knowledge.
- Bad, because the policy diverges between ports.

### Re-feed data

#### A. Retain until first output

- Good, because it costs almost no memory, and seek tickets make the later reseek safe.
- Bad, because a mid-stream fallback shows a short hitch and may refetch data.

#### B. Always keep the current group of pictures

- Good, because a mid-stream fallback is seamless.
- Bad, because a 4K or long group of pictures is large on 1 GB-class devices.

#### C. No retention

- Good, because it is the simplest.
- Bad, because startup fallbacks refetch data too.

### Dropped-frame threshold and attribution

#### A. Attributed drops, 10 % over 5 s

- Good, because compositor stalls never swap a decoder.
- Bad, because the thresholds stay provisional until M11's measurements.

#### B. Any drops, 5 % over 2 s

- Good, because it reacts quickly.
- Bad, because it blames the decoder for renderer and compositor stalls, and switching then makes playback worse.

#### C. Statistics only in v1

- Good, because it carries no risk.
- Bad, because slow decoders keep stuttering.

### Later fallback paths

#### A. Record the guardrails now

- Good, because the rules are clear before those ports start, at no cost now.
- Bad, because D-16 and D-27 inherit the constraints.

#### B. Defer to their milestones

- Good, because no policy is set early.
- Bad, because three later definitions start without guidance.

#### C. Allow MSE to WebCodecs rebuilds

- Good, because it adds resilience against unreliable MSE implementations.
- Bad, because the rebuild is visible and complex, and it goes beyond ADR-0004's load-time selection.

### Blocklist

#### A. Repository data plus app overrides

- Good, because it is auditable and reproducible, integrators can work around a device at once, and it respects
  ADR-0020.
- Bad, because built-in fixes wait for a release.

#### B. Repository data only

- Good, because it is the simplest.
- Bad, because integrators cannot work around a device before a release.

#### C. Remotely updatable list

- Good, because fixes arrive quickly.
- Bad, because it adds a network fetch and a trust model, which ADR-0020 allows only as an explicit opt-in.

## References

- [D-06 discussion](https://github.com/LuisAhumadaMartens/alumbra/issues/6)
- [ADR-0004](0004-web-output-hybrid-mse-webcodecs.md), [ADR-0014](0014-dependency-and-licensing-policy.md),
  [ADR-0016](0016-penumbra-port-contract.md), [ADR-0017](0017-settings-schema-and-configuration.md),
  [ADR-0018](0018-error-model-and-recovery.md), [ADR-0020](0020-observability-logging-tracing-qoe-metrics.md),
  [ADR-0021](0021-testing-strategy.md)
- [Port contract](../architecture/port-contract.md), [media types](../architecture/media-types.md)
- [Media3: MediaCodecSelector](https://developer.android.com/reference/androidx/media3/exoplayer/mediacodec/MediaCodecSelector)
- [Media3: MediaCodecUtil (decoder ordering)](https://developer.android.com/reference/androidx/media3/exoplayer/mediacodec/MediaCodecUtil)
- [Media3: DefaultRenderersFactory (`setEnableDecoderFallback`, extension renderer modes)](https://developer.android.com/reference/androidx/media3/exoplayer/DefaultRenderersFactory)
- [Media3: MediaCodecRenderer (decoder initialisation fallback)](https://developer.android.com/reference/androidx/media3/exoplayer/mediacodec/MediaCodecRenderer)
- [Media3: MediaCodecVideoRenderer (device workarounds)](https://developer.android.com/reference/androidx/media3/exoplayer/video/MediaCodecVideoRenderer)
- [Android: MediaCodecInfo.VideoCapabilities](https://developer.android.com/reference/android/media/MediaCodecInfo.VideoCapabilities)
- [Chromium: media/filters/decoder_selector.h](https://chromium.googlesource.com/chromium/src/+/main/media/filters/decoder_selector.h)
- [Chromium: media/filters/decoder_stream.cc (buffers kept until the first decoded frame)](https://chromium.googlesource.com/chromium/src/+/main/media/filters/decoder_stream.cc)
- [Chromium: media/mojo/services/video_decode_perf_history.cc (smoothness history and threshold)](https://chromium.googlesource.com/chromium/src/+/main/media/mojo/services/video_decode_perf_history.cc)
- [Chromium: gpu/config/gpu_driver_bug_list.json](https://chromium.googlesource.com/chromium/src/+/main/gpu/config/gpu_driver_bug_list.json)
  and [software_rendering_list.json](https://chromium.googlesource.com/chromium/src/+/main/gpu/config/software_rendering_list.json)
- [Cobalt: starboard/player.h (`kSbPlayerErrorCapabilityChanged`)](https://github.com/youtube/cobalt/blob/main/starboard/player.h)
- [mpv manual: `--hwdec`](https://mpv.io/manual/stable/#options-hwdec)
- [FFmpeg wiki: HWAccelIntro](https://trac.ffmpeg.org/wiki/HWAccelIntro)
- [Apple: VTIsHardwareDecodeSupported](https://developer.apple.com/documentation/videotoolbox/vtishardwaredecodesupported(_:))
- [W3C: Media Capabilities, `decodingInfo()`](https://www.w3.org/TR/media-capabilities/#dom-mediacapabilities-decodinginfo)
- [W3C: WebCodecs, hardware acceleration](https://www.w3.org/TR/webcodecs/#hardware-acceleration)
- [W3C: Encrypted Media Extensions, robustness](https://www.w3.org/TR/encrypted-media-2/#dom-mediakeysystemmediacapability-robustness)
