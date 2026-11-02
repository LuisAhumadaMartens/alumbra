# 0045 — Render Apple playback from sample buffers and build an AVPlayer capability in M7

- **Status:** Accepted
- **Date:** 2026-11-01
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #20

## Context

Apple offers two playback models. `AVPlayer` takes a URL and does everything: HLS and LL-HLS, FairPlay through
`AVContentKeySession`, ABR, subtitles, AirPlay video handoff (`allowsExternalPlayback`), picture-in-picture and HLS
Interstitials through `AVPlayerInterstitialEventController` (iOS and tvOS 15 and later). It does not play DASH,
accepts no appended segments and hides demuxing, ABR and buffering. **Sample-buffer rendering** feeds
`CMSampleBuffer`s to `AVSampleBufferDisplayLayer` or `AVSampleBufferVideoRenderer` (iOS and tvOS 17 and later) and
to `AVSampleBufferAudioRenderer`, all slaved to an `AVSampleBufferRenderSynchronizer`. Umbra demuxes, the system
decodes and renders, and FairPlay keys attach to the samples.

The choice decides whether Umbra's value survives on Apple platforms: its own demuxing, ABR, text and ads, DASH,
non-HTTP protocols and conformance testing. It gates M7.

Settled elsewhere and not reopened here:

- **Floors** ([ADR-0039](0039-compatibility-matrix-and-support-tiers.md)). iOS, iPadOS and tvOS 17 and macOS 14. One renderer API, `AVSampleBufferVideoRenderer`,
  so the display-layer fallback for older systems is not needed.
- **Clock** ([ADR-0024](0024-media-clock-and-av-sync.md)). The sample-buffer path synchronises in the platform and
  reports media time through `GetSnapshot`. The audio renderer and the render synchroniser are the master.
- **Port contract** ([ADR-0016](0016-penumbra-port-contract.md)). A port may implement the player contract
  directly, and capabilities are declared per port.
- **Decoder rules** ([ADR-0029](0029-decoder-selection-and-fallback.md)). A secure stream never falls back to a
  non-secure decoder.
- **DRM staging** ([ADR-0005](0005-drm-scope.md)). FairPlay arrives through the platform path in M9.

Two facts shape the AirPlay question. AirPlay video to an Apple TV, as opposed to mirroring or an audio route, is
an `AVPlayer` capability, and whether any other API reaches it is unverified. IMA iOS SGAI uses HLS Interstitials
through `useHLSInterstitials` on the stream request, and the SDK checks device compatibility only with
`IMAAVPlayerVideoDisplay`.

## Decision drivers

- Umbra's demuxing, ABR, text, ads and DASH keep working on Apple platforms, with full conformance testing.
- AirPlay video handoff works at launch, because casting to an Apple TV is a basic expectation on Apple devices.
- One reference path that every other port can be compared with.
- APIs a path cannot honour fail visibly, not silently.
- Fewest surprises: the choice of path is reported, not hidden.

## Considered options

1. **A — Sample-buffer pipeline only.** Matches the other ports and supports DASH, but has no AirPlay video
   handoff and no native interstitials.
2. **B — `AVPlayer` URL path only.** Fastest route to App Store-grade HLS, FairPlay, AirPlay and SGAI, but no DASH,
   behaviour that diverges from other ports and opaque metrics.
3. **C — Hybrid.** The sample-buffer pipeline by default and as the reference, with `AVPlayer` as a scoped
   capability for sessions that need what only it provides.

AirPlay video in M7, a sub-question of C:

1. **No.** M7 ships AirPlay audio and mirroring, and the `AVPlayer` capability arrives with M10.
2. **Yes.** The `AVPlayer` capability is built in M7.

## Decision

Chosen option: **C, and the `AVPlayer` capability is built in M7** so AirPlay video handoff works at launch. This
departs from the recommendation to defer it. It makes M7 larger, because it adds a second playback stack and an
API-gap policy to the same milestone, and the maintainer accepts that cost for full AirPlay support.

### Two paths

- The **sample-buffer pipeline** is the default and the conformance reference. Penumbra enqueues compressed samples
  for every codec the system can render. FairPlay content is always enqueued compressed.
- Penumbra uses VideoToolbox or dav1d only when Umbra needs decoded frames, for example AV1 where the device has no
  hardware decoder. Which devices need it is unverified.
- The renderer is `AVSampleBufferVideoRenderer`.
- **`AVPlayer` is a capability, not a mode.** It is declared as a URL-player capability in the port's capability
  set, and it works on HLS sessions only.

### Choosing a path

- `playback.path` (`auto`, `pipeline` or `avplayer`) selects the path, defaulting to `auto`. The issue's
  `playback.apple.path` is renamed because ADR-0017 keeps platform names out of keys.
- `auto` selects `AVPlayer` for HLS sessions that need AirPlay video, and later for HLS interstitials and IMA SGAI.
  Every other session uses the pipeline.
- Any API that `AVPlayer` cannot honour, such as custom ABR or manual rendition selection, fails visibly with a
  structured error (ADR-0018) rather than being ignored.
- The decision, with its reason, is reported in the stats overlay and in an event.
- On `AVPlayer` sessions only some HTMLMediaElement-shaped members are emulated. The API documentation lists which
  members each path supports.

### Clock and stats

The audio renderer and `AVSampleBufferRenderSynchronizer` are the master (ADR-0024's audio master). Umbra reads
their position and the system presents the video. Umbra's per-vsync cadence estimator is therefore inactive on this
path, and statistics come from the renderer.

### tvOS and shared services

- tvOS sets display criteria for frame rate and dynamic range, consistent with ADR-0030's rate-matching follow-up.
  The output-obscured state is exposed as a key status (D-27).
- Now Playing and remote commands (`MPNowPlayingInfoCenter`, `MPRemoteCommandCenter`) are one implementation shared
  by both paths.
- The migration from `AVSampleBufferAttachContentKey` to the `CMReadySampleBuffer` content-key API is planned with
  M9. Its API details are unverified.

## Consequences

**Positive**
- AirPlay video handoff works at launch, and Umbra's features and DASH remain on the default path.
- The pipeline stays the reference for conformance, so behaviour matches the other ports.
- The `AVPlayer` capability is ready for HLS interstitials and IMA SGAI in M10.

**Negative / costs**
- M7 carries nearly a second playback stack, with its own events, errors and buffered ranges kept consistent with
  the first, and two paths to test.
- `AVPlayer` sessions give less control: no custom ABR, no manual rendition selection, black-box metrics.
- Alumbra reimplements what Apple's player does well on the pipeline path: HDR and Dolby Vision display matching,
  tvOS frame-rate matching and output-protection signalling.

**Follow-ups**
- #204 builds the video path per this decision: the sample-buffer renderer, the VideoToolbox and dav1d cases and
  the path-selection rules.
- #205 builds the audio path and `AVAudioSession`, with the synchroniser as master.
- #212 builds picture-in-picture, AirPlay and Now Playing. AirPlay now includes video handoff through `AVPlayer`.
- A new M7 task builds the `AVPlayer` capability: URL-player declaration in the port capability set, `auto`
  selection, the API-gap errors and the HTMLMediaElement-shaped emulation table.
- A new M7 task builds AirPlay video handoff on that capability, with a conformance case on a reference device.
- A new M7 task reports the path decision in the stats overlay and in an event.
- M9 plans the `CMReadySampleBuffer` migration. M10 SGAI (#255) uses the capability for HLS interstitials on Apple
  platforms.
- `docs/roadmap.md` M7 line is clarified: AirPlay video handoff goes through the `AVPlayer` capability built in M7.
- Settings added to the schema under ADR-0017 naming: `playback.path`.

## Pros and cons of the options

### A — Sample-buffer only
- Good, because it is the same on every port, with full conformance and DASH.
- Bad, because there is no AirPlay video handoff and no native interstitials.

### B — AVPlayer only
- Good, because it is the fastest route to App Store-grade HLS, FairPlay, AirPlay and SGAI.
- Bad, because there is no DASH, behaviour diverges from other ports and metrics are opaque.

### C — Hybrid, capability built in M7
- Good, because it keeps Umbra's value and delivers AirPlay video at launch.
- Bad, because it is the highest engineering cost and two paths must stay consistent.

### AirPlay video in M7 versus M10
- Waiting until M10 is good for a smaller M7, and bad because casting video to an Apple TV is missing until then.
- Building it in M7 is good for full AirPlay support at launch, and bad for a larger milestone.

## References

- [Apple: AVSampleBufferDisplayLayer](https://developer.apple.com/documentation/avfoundation/avsamplebufferdisplaylayer)
- [Apple: AVSampleBufferVideoRenderer](https://developer.apple.com/documentation/avfoundation/avsamplebuffervideorenderer)
- [Apple: AVSampleBufferRenderSynchronizer](https://developer.apple.com/documentation/avfoundation/avsamplebufferrendersynchronizer)
- [Apple: AVContentKeySession](https://developer.apple.com/documentation/avfoundation/avcontentkeysession)
- [Apple: AVPictureInPictureController](https://developer.apple.com/documentation/avkit/avpictureinpicturecontroller)
- [Apple: AVPlayerInterstitialEventController](https://developer.apple.com/documentation/avfoundation/avplayerinterstitialeventcontroller)
- [IMA DAI iOS: IMAVideoDisplay](https://developers.google.com/ad-manager/dynamic-ad-insertion/sdk/ios/reference/Protocols/IMAVideoDisplay)
- [Cobalt: tvOS url_player.h](https://github.com/youtube/cobalt/blob/main/starboard/tvos/shared/media/url_player.h)
- [WebKit: MediaPlayerPrivateMediaSourceAVFObjC.mm](https://github.com/WebKit/WebKit/blob/main/Source/WebCore/platform/graphics/avfoundation/objc/MediaPlayerPrivateMediaSourceAVFObjC.mm)
- [ADR-0005](0005-drm-scope.md), [ADR-0016](0016-penumbra-port-contract.md),
  [ADR-0024](0024-media-clock-and-av-sync.md), [ADR-0029](0029-decoder-selection-and-fallback.md)
- [Research: Cobalt and Starboard](../research/cobalt-and-starboard.md),
  [research: SSAI and SGAI](../research/ssai-and-sgai.md)
