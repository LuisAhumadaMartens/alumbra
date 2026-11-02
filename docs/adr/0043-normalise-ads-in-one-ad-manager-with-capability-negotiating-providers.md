# 0043 — Normalise ads in one AdManager with capability-negotiating providers

- **Status:** Accepted
- **Date:** 2026-11-01
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #25

## Context

D-25 fixes how Umbra's vendor-neutral AdManager connects to three IMA SDKs and to the built-in VAST and VMAP
provider that desktop uses. [ADR-0008](0008-ads-architecture.md) chose the AdManager and one provider per platform;
this decision sets the playhead, the events, errors, consent and, above all, who plays the ad media. M5 builds the
web and desktop parts. The Android and Apple parts are recorded now because they shape the interfaces.

The three SDKs differ:

- **IMA HTML5** plays ads in its own `<video>` and accepts any object that exposes `currentTime` as the content
  playhead. The app pauses content on `CONTENT_PAUSE_REQUESTED`, resumes it on `CONTENT_RESUME_REQUESTED` and calls
  `adsLoader.contentComplete()` for post-rolls. `AdDisplayContainer.initialize()` needs a user gesture on mobile.
- **IMA Android** lets the app implement `VideoAdPlayer` (`loadAd`, `playAd`, `pauseAd`, `stopAd`, `getAdProgress`),
  so Alumbra itself plays the ad media, as Media3 does. Since 3.16.2 it reads the IAB TCF string from shared
  preferences. Which signals each SDK reads itself is unverified per SDK.
- **IMA iOS and tvOS** play ads themselves and take a content playhead. Their DAI accepts a custom
  `IMAVideoDisplay`, which matters for D-20 and D-26.

VMAP and ad-rules cue points are scheduled by the SDKs from the playhead. Errors carry VAST codes, and an ad
failure must never become a content `MediaError`.

Settled elsewhere and not reopened here:

- **Content time during client-side breaks** ([ADR-0023](0023-public-api-surface-and-media-error.md)). The element
  keeps reporting the content, and ad playback state lives in the ads module.
- **Port contract** ([ADR-0016](0016-penumbra-port-contract.md)). Penumbra ports pull data and own decoder pacing.
- **Decoder fallback** ([ADR-0029](0029-decoder-selection-and-fallback.md)). One decoder chain serves content and
  ad media on a port that plays ads itself.
- **Layering** ([ADR-0042](0042-keep-player-chrome-off-the-ad-and-reduce-it-to-an-ad-band.md)). The ad container and
  band contract, including the obstruction registry, belongs to D-23.

## Decision drivers

- Vendor neutrality: one event model, one set of UI rules and one error policy on every platform.
- IMA features stay reachable without every app coding against three SDKs.
- Single-decoder TVs keep working through ad breaks, with the least memory.
- Ads never break content playback.
- Quick development: use each SDK's normal integration where one exists.

## Considered options

1. **A — Thin platform passthrough.** Apps use each IMA SDK directly. Least code, but no shared events or rules.
2. **B — Normalised AdManager with capability-negotiating providers.** One contract, with providers declaring what
   they support.
3. **C — Player-rendered ads wherever possible.** B, with a policy of rendering ad media through Umbra wherever an
   SDK allows. Best for single-decoder devices, most work.

Android ad decoding, a sub-question of B:

1. **A — Same pipeline, in place.** `VideoAdPlayer` feeds a second source into the one player and codec.
2. **B — Second player instance.** Fastest switches where two decoders exist, but it fails on single-decoder TVs.

## Decision

Chosen option: **B**, using C's player-rendered mode where it is the platform norm. On Android, ads play **in the
same pipeline, in place**.

### Providers and ports

The AdManager defines three ports: `ContentPlayhead` (content time, duration and the content-complete signal),
`AdMediaPlayer` (optional, for providers that let the player render ads) and `AdUiHost` (the container and the
obstruction registry from ADR-0042). Providers declare capabilities such as `rendersOwnAdMedia`,
`supportsCuePoints`, `supportsCompanions`, `supportsSkipFocus` and `requiresUserGestureInit`.

Who plays the ad media:

| Platform | Ad media played by |
|---|---|
| Web | The IMA element |
| Android | Alumbra, through `VideoAdPlayer` |
| iOS and tvOS | The IMA SDK |
| Desktop | The built-in provider |

The raw IMA object is exposed as an `unstable` extension for features outside the contract.

### Web

Ads force the MSE content path ([ADR-0036](0036-web-output-selection-and-ffmpeg-wasm-addon.md)), because IMA is MSE-only. DAI is MSE-only as well (D-26). On iPhone,
custom playback is turned off per [ADR-0040](0040-fullscreen-and-picture-in-picture.md), so skippable ads work.

### Android: same pipeline, in place

`VideoAdPlayer` feeds a second source into the one Umbra player and decoder. The pipeline pre-buffers the ad and
reconfigures the decoder without a flush where codec parameters allow, otherwise it flushes in a controlled way.
It works on single-decoder TVs and uses less memory. A short stall at a transition is accepted. A second player
instance is not used.

### Playhead

One `ContentPlayhead` in Umbra reports content time, as ADR-0023 decided. On the web it is a `currentTime` proxy
object, so the WebCodecs path (ADR-0004) can serve it, although ads force MSE.

### Events and errors

- Normalised events: loaded, started, quartiles, complete, skipped, clicked, paused, resumed, progress, break
  started, break ended, all-ads-complete and error. Each carries the ad id, creative id, pod index and size,
  duration and skip offset where they apply.
- VAST and IMA error codes pass through in `adError` with the provider's code preserved. Content always resumes.

### Snapback

The AdManager owns snapback so desktop and IMA behave alike. `ads.seek.play_skipped_breaks` (`last`, `all` or
`none`) defaults to `last`, and D-26 shares it for stitched and server-guided breaks.

### Consent and configuration

- IMA reads the CMP storage (TCF) itself where it can. Alumbra passes GPP, the limited-ads flag, the
  child-directed flag and the PPID through settings only where the SDK does not read them itself.
- The next ad loads ahead of the break, inside the ad budget that D-12 (#12) defines.
- Companions are out of M5.

### Built-in provider in M5

VAST 4.x inline and wrapper responses, linear media selection, skippable ads, tracking and error macros, and VMAP
pre-, mid- and post-rolls. VPAID, OMID and companions are not included.

## Consequences

**Positive**
- Apps see one event model, one playhead and one error policy on every platform.
- Android and desktop play ads on one decoder and surface, so single-decoder TVs work and PiP and fullscreen stay
  continuous.
- IMA stays reachable through the `unstable` escape hatch.

**Negative / costs**
- Alumbra owns ad-media behaviour on Android and desktop that the SDK would otherwise handle.
- An in-place ad transition can stall briefly on Android.
- A normalised contract trails new IMA features until the contract grows.
- Each provider must be tested against ad-policy rules, since the player renders ads on two platforms.

**Follow-ups**
- #162 builds the ad break and pod model and the scheduling, including the snapback rule.
- #163 defines the provider interface, the capabilities and the lifecycle events.
- #173 builds the IMA SDK loading and the web provider adapter, with the `currentTime` proxy.
- #178 implements styled fullscreen during ad breaks.
- #196 (M6) builds the IMA Android provider on `VideoAdPlayer` in the same pipeline, and measures the transition
  stall on a single-decoder device.
- #214 (M7) builds the IMA iOS and tvOS provider.
- Settings added to the schema under ADR-0017 naming: `ads.seek.play_skipped_breaks`, `ads.ima.locale`,
  `ads.ima.ppid`, `ads.preload.enabled`, `ads.consent.tcf_string`, `ads.consent.gpp_string`,
  `ads.consent.limited_ads.enabled`, `ads.consent.child_directed.enabled` and `ads.ima.debug.enabled`. The issue's
  `ads.preload` and `ads.ima.debug` are renamed to the positive boolean form.
- Which signals each IMA SDK reads itself is unverified and is checked in #173, #196 and #214.

## Pros and cons of the options

### A — Passthrough
- Good, because it is the least code and gives immediate access to new IMA features.
- Bad, because there is no vendor neutrality, no shared events and every app enforces the UI rules itself.

### B — Normalised AdManager
- Good, because events, rules and errors are the same everywhere and it matches Shaka Player's AdManager.
- Bad, because each provider needs work, and new IMA features arrive through the escape hatch first.

### C — Player-rendered where possible
- Good, because one decoder serves everything on TVs.
- Bad, because Alumbra takes on ad-media behaviour and policy risk, and on the web it applies only to custom
  playback.

### Android: same pipeline versus second instance
- Same pipeline is good for single-decoder TVs and memory, and bad for a short stall at transitions.
- A second instance is good for fast switches, and bad because it fails on single-decoder TVs and doubles memory
  and effort.

## References

- [IMA HTML5: Get started](https://developers.google.com/interactive-media-ads/docs/sdks/html5/client-side)
- [IMA HTML5: AdsManager](https://developers.google.com/interactive-media-ads/docs/sdks/html5/client-side/reference/js/google.ima.AdsManager)
- [IMA Android: VideoAdPlayer](https://developers.google.com/interactive-media-ads/docs/sdks/android/client-side/api/reference/com/google/ads/interactivemedia/v3/api/player/VideoAdPlayer)
- [IMA Android: Consent](https://developers.google.com/interactive-media-ads/docs/sdks/android/client-side/consent)
- [IMA iOS: IMAContentPlayhead](https://developers.google.com/interactive-media-ads/docs/sdks/ios/client-side/reference/Protocols/IMAContentPlayhead)
- [IAB Tech Lab: VAST 4](https://iabtechlab.com/standards/vast/)
- [IAB Tech Lab: VMAP](https://iabtechlab.com/standards/video-multiple-ad-playlist-vmap/)
- [Media3: Ad insertion](https://developer.android.com/media/media3/exoplayer/ad-insertion)
- [Shaka Player: shaka.ads.AdManager](https://shaka-player-demo.appspot.com/docs/api/shaka.ads.AdManager.html)
- [ADR-0008](0008-ads-architecture.md), [ADR-0016](0016-penumbra-port-contract.md),
  [ADR-0023](0023-public-api-surface-and-media-error.md), [ADR-0029](0029-decoder-selection-and-fallback.md)
