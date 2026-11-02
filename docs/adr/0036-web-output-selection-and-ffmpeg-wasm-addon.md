# 0036 — Select the web output path by probe and add an on-demand FFmpeg WASM add-on for full compatibility

- **Status:** Accepted
- **Date:** 2026-11-01
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #16

## Context

[ADR-0004](0004-web-output-hybrid-mse-webcodecs.md) chose hybrid output on the web. This ADR defines the executable
rule and the probes. MSE keeps hardware decode, EME, PiP, IMA and native fullscreen. WebCodecs adds frame control
but loses those. D-16 blocks #147, #148, #149 and #151.

The maintainer's goal is that the player is fully compatible on the web: it plays what the device has any chance to
play. Weak devices matter, because phones and TVs are the common case. Old devices do not.

Browser codecs leave real gaps. Some containers have no MSE support, and some audio and video codecs have no
browser decoder: AC-3, E-AC-3, DTS, TrueHD and MP2 audio, and codecs a given engine lacks. Software decoding in
WASM fills those gaps, but it conflicts with [ADR-0014](0014-dependency-and-licensing-policy.md), whose web row says
"No LGPL code in the WASM bundle or a shipped side module." FFmpeg's decoders are LGPL. Single-threaded WASM cannot
hold 1080p60 on many phones and TVs, so threads matter here (D-17,
[ADR-0037](0037-wasm-threading-and-cross-origin-isolation.md)).

[ADR-0029](0029-decoder-selection-and-fallback.md) allows no MSE-to-WebCodecs rebuild mid-stream.

## Decision drivers

- Hardware decode first, for power and smoothness.
- Full compatibility as a stated goal, without paying its size and licence cost in the core bundle.
- Weak devices matter. The player must say clearly when one cannot keep up.
- Keep PiP, IMA and EME wherever MSE can play the stream.
- Keep the core WASM bundle free of LGPL, as ADR-0014 requires for Apple and as it set for the web.

## Considered options

WASM software decode:

1. **Out of M4**: browser codecs only, with dav1d and FFmpeg WASM later.
2. **dav1d only**: AV1 in WASM for browsers lacking it.
3. **Full FFmpeg WASM** in the core bundle.
4. **A layered fallback** ending in an on-demand FFmpeg add-on. This is the chosen refinement of option 1 and 3.

MSE quality floor:

1. **MSE unless it can play nothing**: if MSE plays any rendition, ABR filters out the rest.
2. **MSE must cover the top rung**: WebCodecs otherwise.

## Decision

Chosen options: **a layered fallback for full compatibility, and MSE unless it cannot play any rendition.** The
choice is decided once per presentation, before the first frame, and never switched mid-stream. A restart after the
first frame is error recovery, not a path switch.

### The layers

1. **Hardware first.** MSE, then WebCodecs. This is the default and covers most streams.
2. **Container gaps.** Alumbra's own C++ demuxers, running in WASM, remux the stream to fMP4 with no decoding, and the
   result feeds MSE. This ships in M4 and adds no licence cost.
3. **Audio codec gaps** (AC-3, E-AC-3, DTS, TrueHD, MP2 and others). FFmpeg audio decoders in WASM produce PCM for
   the Web Audio sink (#149).
4. **Video codec gaps.** FFmpeg video decoders in WASM, threaded and using SIMD where the page allows it, as a last
   resort. The player reports clearly when a weak device cannot keep up, using the drop trigger of ADR-0029 and a cap
   on ABR (D-11), and it reports it through the error and warning model, not silently.

Layers 3 and 4 live in an on-demand add-on, **`alumbra-ffmpeg`**. It is downloaded only when a stream needs it, and
lands right after M4.

### Selection rules

- Hard rules come first: encrypted content means MSE, an active IMA provider means MSE unless the integrator
  overrides it, HDR means MSE (a canvas cannot match it), and iPhone uses `ManagedMediaSource` with
  `disableRemotePlayback`.
- Probes: `MediaSource.isTypeSupported`, `MediaCapabilities.decodingInfo` with the real frame rate (this matters
  above 60 fps), and `VideoDecoder.isConfigSupported`. Results are cached per session and shown in stats. Device
  runs feed the D-22 matrix ([ADR-0039](0039-compatibility-matrix-and-support-tiers.md)).
- `ManagedMediaSource` is used only where `MediaSource` is missing (as hls.js does by default), which keeps eviction
  paths few.
- Audio on the WebCodecs path in Safari before 26, which lacks `AudioDecoder`, is unsupported, because the D-22 Safari
  floor is 17.1 and the WebCodecs-only path adds little there. With the add-on, WASM audio decoders cover it.
- MSE is preferred partly because the compositor presents its frames. Canvas presentation relies on page pacing.
  That difference is unverified and #147 and #148 measure it.

### What layers 3 and 4 give up

- They run on the WebCodecs-style path: decoded audio to an AudioWorklet and frames to a canvas. They therefore lose
  what that path loses: native fullscreen, IMA, AirPlay and EME. PiP works only through Document PiP. The player
  reports each gap as a capability warning.
- Encrypted content cannot use them, since decryption stays in the browser's CDM. A stream that needs the add-on
  and is encrypted fails with a clear error.
- A mix, such as WASM audio with MSE video, is not designed here. #151 decides whether it is worth the
  clock-synchronisation cost.

### The add-on and ADR-0014

This ADR supersedes ADR-0014's web restriction for this one module only:

- `alumbra-ffmpeg` is a separately loaded, separately versioned and replaceable WASM module. The core WASM bundle
  stays free of LGPL code.
- Apple artefacts stay LGPL-free. The add-on is not built for them.
- The ADR-0014 conditions apply: a review entry with exact version, features and build flags, a narrow decoder-only
  build, LGPL notices, complete corresponding source and build scripts alongside the binary, and a documented
  replacement procedure. Replacement means an integrator can point `decode.addon_url` at their own build with a
  compatible interface.
- Whether a replaceable WASM module satisfies LGPL section 6 in practice is unverified and needs a legal read
  before release. If it does not, the add-on does not ship, and layers 3 and 4 fall back to a clear "unsupported
  codec" error.
- The add-on is fetched from the integrator's hosting by default and checked against an integrity hash. Alumbra
  fetches nothing from a third-party host on its own.

### Settings

Keys follow ADR-0017, where platforms never appear in a key, so the brief's `web.*` names change:
`engine.output_path` (`auto`, `mse`, `webcodecs`), `decode.software_max_height`, `decode.addon_enabled` and
`decode.addon_url`.

## Consequences

**Positive**
- Hardware decode stays the default, and PiP, ads and DRM keep working wherever MSE can play.
- The player can play what the device can play, including audio codecs browsers lack.
- The core bundle stays small and LGPL-free.

**Negative / costs**
- A new module to build, size-budget, review for licence and keep in step with the core.
- Software video on weak devices can fail to keep up. The player says so, but it cannot fix it.
- The first play of such a stream waits for the add-on download.
- Layers 3 and 4 lose PiP, IMA and EME, as the WebCodecs path does today.

**Follow-ups**
- #147 builds the MSE path, #148 the WebCodecs and WebGL path, #149 the Web Audio sink and #151 probing and
  selection. #151 implements the rules above, and decides the mixed-path question.
- New task: build the `alumbra-ffmpeg` WASM add-on, with a narrow FFmpeg decoder build, SIMD, a threaded variant
  (depends on [ADR-0037](0037-wasm-threading-and-cross-origin-isolation.md)), an on-demand loader and a size budget.
  It lands right after M4 and needs its own milestone entry.
- New task: ADR-0014 review entry and legal read for the add-on, before it ships (D-32 process).
- New task: layer 2, the C++ remux-to-fMP4 path, in M4 under #147.
- Add the four settings above to the schema (#151).
- Update `docs/compatibility.md` codec cells for web (AC-3, E-AC-3 and others) when the add-on ships.
- D-22 matrix runs record probe results per device (#156).

## Pros and cons of the options

### Out of M4
- Good, because it keeps M4 small.
- Bad, because some streams would not play at all.

### dav1d only
- Good, because it is C++ and moderate in size.
- Bad, because it covers only AV1, and only at low resolutions on weak devices.

### Full FFmpeg in the core bundle
- Good, because it plays almost anything.
- Bad, because the bundle is large and LGPL, which ADR-0014 does not allow in the core.

### Layered fallback with an on-demand add-on
- Good, because users pay for software decode only when a stream needs it, and the core stays clean.
- Bad, because it adds a module, a licence review and a longer first play for those streams.

### MSE unless it plays nothing
- Good, because it keeps PiP, ads and power efficiency.
- Bad, because the top rung may be unavailable.

### MSE must cover the top rung
- Good, because it keeps the quality ceiling.
- Bad, because it trades hardware-efficient playback and ad support for software paths.

## References

- [Media Source Extensions](https://www.w3.org/TR/media-source-2/) and
  [Managed Media Source](https://w3c.github.io/media-source/#managedmediasource-interface)
- [WebCodecs](https://www.w3.org/TR/webcodecs/) and [Media Capabilities](https://www.w3.org/TR/media-capabilities/)
- [FFmpeg legal checklist](https://ffmpeg.org/legal.html) and
  [FFmpeg licensing notes](https://github.com/FFmpeg/FFmpeg/blob/master/LICENSE.md)
- [GNU LGPL 2.1, section 6](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html)
- [hls.js](https://github.com/video-dev/hls.js), for `ManagedMediaSource` use
- [ADR-0004](0004-web-output-hybrid-mse-webcodecs.md), [ADR-0014](0014-dependency-and-licensing-policy.md),
  [ADR-0016](0016-penumbra-port-contract.md), [ADR-0017](0017-settings-schema-and-configuration.md),
  [ADR-0025](0025-demuxing-strategy.md), [ADR-0029](0029-decoder-selection-and-fallback.md)
