# 0047 — Shape DRM like EME and ship PlayReady on Edge and Android TV

- **Status:** Accepted
- **Date:** 2026-11-01
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #27

## Context

D-27 decides one content-decryption abstraction for Umbra and Penumbra and the public SDK surface on top of it.
[ADR-0005](0005-drm-scope.md) stages DRM: ClearKey with CENC in Umbra's own code first, then Widevine, FairPlay and
PlayReady through each platform's native path. A C++ engine cannot decrypt commercial DRM content itself, so it
routes encrypted samples and licence exchanges to the platform. The abstraction has to fit both stages.

Two shapes already exist. Encrypted Media Extensions (EME) is the web's session model, and Starboard's `SbDrm`
mirrors it (`SbDrmCreateSystem`, `SbDrmGenerateSessionUpdateRequest`, `SbDrmUpdateSession`, key-status callbacks and
server certificates). Starboard's `SbPlayerWriteSamples` carries the scheme, pattern, IV, key ID and subsample map
with each sample. Safari exposes FairPlay as `com.apple.fps` through EME, with a server certificate and `skd` init
data, which shows an EME-shaped port can cover FairPlay.

Settled elsewhere and not reopened here:

- **ClearKey schemes** ([ADR-0005](0005-drm-scope.md)). ClearKey supports both `cenc` and `cbcs`.
- **Encryption metadata** ([ADR-0016](0016-penumbra-port-contract.md)). Samples carry full encryption metadata from
  the first version, and Umbra never decrypts platform-CDM content.
- **No insecure fallback** ([ADR-0029](0029-decoder-selection-and-fallback.md)). A secure stream never falls back to
  a non-secure decoder.
- **Threading** ([ADR-0015](0015-threading-and-task-runners.md)). Nothing blocks the control sequence, so licence
  hooks are asynchronous.
- **Errors** ([ADR-0018](0018-error-model-and-recovery.md)). DRM errors use the 5xxx range of the catalogue, mapped
  to `MediaError`.
- **Windows** ([ADR-0031](0031-windows-support-scope-and-toolchain.md)). Windows 11 builds the engine only, and
  playback is deferred, so native Windows PlayReady waits for the Windows playback milestone.

## Decision drivers

- One shape across platforms, so a fake CDM can drive the same licence-flow tests everywhere.
- Integrators describe DRM in configuration and hooks, not per-platform session code.
- The web port is a thin adapter over the browser's own EME.
- Wide TV coverage, without making any platform's CDM a hard dependency.
- Secure content never plays on a path that cannot protect it.

## Considered options

1. **A — EME-shaped internal port on every platform.** Full flexibility, medium effort for app developers.
2. **B — Platform-native DRM per SDK.** No abstraction, duplicated licence logic and no shared tests.
3. **C — Config-first public API over an internal EME-shaped port.** Declarative licence servers and hooks, with
   the raw session API as an escape hatch.

PlayReady scope, a sub-question:

1. **Web only,** through Edge's EME.
2. **Web plus Android TV,** through `MediaDrm` where the device supports it.
3. **Web plus native Windows 11,** only after the Windows playback milestone.

## Decision

Chosen option: **A as the Penumbra contract and C as the public SDK API**, with **PlayReady on the web (Edge,
through EME) and on Android TV (through `MediaDrm`, where the device supports it)**. Option A fits ClearKey now and
platform CDMs later, and C keeps application code short. PlayReady on Android TV widens TV coverage over the
web-only choice, at the price of more per-device checks.

### Penumbra contract

- A `DrmSystem` port models EME: `CreateSession`, `GenerateRequest(initDataType, initData)`, an `OnMessage`
  callback, `Update`, `Close` and `SetServerCertificate`, with key-status callbacks and capability queries.
- The web port is a thin adapter. Conformance tests in the style of Starboard's NPLB run the same scripts on every
  port (D-31, #31).
- Encrypted samples reach the decoder port with a `SampleEncryptionInfo` (scheme, pattern, IV, key ID,
  subsamples), which the port forwards to the platform CDM.
- **Rotation.** New key IDs arrive per sample, as key ID and IV, so rotation causes no decoder flush when the codec
  is unchanged.
- **Hooks.** Licence hooks are asynchronous on the control sequence and use the licence request class of the
  network stack.

### ClearKey

JSON Web Key licences, with init data types `cenc` and `keyids`. `webm` init data is deferred. ClearKey decrypts in
Umbra before decode, so demuxing and metadata paths are exercised before any commercial CDM.

### Public API

Declarative licence servers (for example `drm.servers.com_widevine_alpha`), request and response filters for
headers, body wrapping and tokens, server certificates, and the `drmSessionUpdate` and `keyStatusChange` events. The
low-level session API stays available for advanced apps.

### Key systems

| Platform | Key systems |
|---|---|
| Web | Widevine; PlayReady in Edge through EME; FairPlay in Safari |
| Android and Google TV | Widevine; PlayReady on Android TV through `MediaDrm` |
| Apple platforms | FairPlay |
| macOS and Linux desktop | ClearKey only; no CDM is available to native desktop (licensing unverified) |
| Windows 11 | Not before the Windows playback milestone |

### PlayReady on Android TV

Android PlayReady support depends on the device. It is unverified for any given model. M9 therefore adds a
per-device capability check through `MediaDrm` and `MediaCodec` secure decoders before PlayReady is offered, and
adds Android TV devices to the DRM test matrix. A device that fails the check reports the key system as
unsupported, and ADR-0029 still prevents any non-secure fallback.

### Capabilities, robustness and errors

- Capabilities are reported per key system, codec, encryption scheme and robustness, and ABR filters out
  renditions the CDM cannot play (D-11).
- Key statuses `output-restricted` and `output-downscaled` are surfaced to ABR as caps.
- Robustness and HDCP requirements are settings and appear in errors, using the ADR-0018 DRM range.

### Out of scope

Persistent and offline licences are out of scope until offline playback exists. This ADR records that.

### Test content

DASH-IF and Axinom test vectors (their terms are unverified), plus an own ClearKey test server.

## Consequences

**Positive**
- One abstraction serves ClearKey now and every platform CDM later, with shared licence-flow tests.
- Integrators configure licence servers and hooks once, and PlayReady reaches both Edge and Android TV.
- The web port is thin, and rotation never flushes the decoder.

**Negative / costs**
- The abstraction can leak on FairPlay's certificate and SPC and CKC flow, and the declarative API hides that.
- PlayReady on Android TV needs per-device checks and a bigger test matrix.
- Desktop gets ClearKey only, and Windows PlayReady waits until the Windows playback milestone.
- No persistent licences, so there is no offline playback yet.

**Follow-ups**
- #235 builds the EME-shaped key-system API, with the declarative servers, filters, certificates, events and
  capability queries.
- #236 builds protection metadata parsing (CENC boxes, HLS keys and DASH `ContentProtection`).
- A new M9 task adds PlayReady on Android TV: the `MediaDrm` per-device capability check and the key-system entry.
- A new M9 task adds Android TV devices and Edge PlayReady content to the DRM test matrix (D-22 data file).
- A new M9 task adds the robustness and HDCP settings, and the `drm.servers.*` keys, to the schema under ADR-0017
  naming. Exact names are fixed in #235, and none may carry a platform name.
- The Windows 11 native PlayReady decision is revisited with the Windows playback milestone and D-35 (#35).
- `docs/roadmap.md` M9 says "PlayReady where available". That now means Edge and Android TV, and a later roadmap
  pass can name them.

## Pros and cons of the options

### A — EME-shaped port
- Good, because it matches EME and Starboard, supports shared tests and fits ClearKey and platform CDMs.
- Bad, because app developers handle sessions, and FairPlay's flow can leak.

### B — Platform-native per SDK
- Good, because there is the least abstraction and full platform access.
- Bad, because licence logic is duplicated, there are no shared tests and Umbra's demux metadata has no common
  consumer.

### C — Config-first over A
- Good, because app effort is low and hooks cover custom protocols.
- Bad, because the abstraction can leak, and a raw escape hatch is needed.

### PlayReady scope
- Web only is good for no native work, and bad because Android TV playback waits.
- Web plus Android TV is good for wider TV coverage, and bad for per-device checks and a larger matrix.
- Web plus Windows 11 is only possible after the Windows playback milestone, so it adds nothing before then.

## References

- [W3C: Encrypted Media Extensions](https://www.w3.org/TR/encrypted-media-2/)
- [W3C: EME Encryption Scheme Query](https://wicg.github.io/encrypted-media-encryption-scheme/)
- [W3C: EME Initialization Data Format Registry](https://www.w3.org/TR/eme-initdata-registry/)
- [Cobalt: starboard/drm.h](https://github.com/youtube/cobalt/blob/27.lts/starboard/drm.h)
- [Cobalt: starboard/media.h](https://github.com/youtube/cobalt/blob/27.lts/starboard/media.h)
- [Android: MediaDrm](https://developer.android.com/reference/android/media/MediaDrm)
- [Android: MediaCodec.CryptoInfo](https://developer.android.com/reference/android/media/MediaCodec.CryptoInfo)
- [Apple: AVContentKeySession](https://developer.apple.com/documentation/avfoundation/avcontentkeysession)
- [DASH-IF: Content protection identifiers](https://dashif.org/identifiers/content_protection/)
- [Shaka Player: DRM configuration](https://shaka-player-demo.appspot.com/docs/api/tutorial-drm-config.html)
- [ADR-0005](0005-drm-scope.md), [ADR-0016](0016-penumbra-port-contract.md),
  [ADR-0029](0029-decoder-selection-and-fallback.md), [ADR-0031](0031-windows-support-scope-and-toolchain.md)
- [Research: Cobalt and Starboard](../research/cobalt-and-starboard.md)
