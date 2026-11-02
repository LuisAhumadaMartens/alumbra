# SSAI and SGAI

Last verified: 2026-10-31

This note defines client-side (CSAI), server-side (SSAI) and server-guided (SGAI) ad insertion as the DASH Industry
Forum and Google use the terms. It shows how Google's IMA DAI products implement SSAI and SGAI on each platform, and
compares the standard SGAI primitives: HLS Interstitials and DASH events, XLink and Alternative MPD. It ends with the
stream-time vs content-time problem and what it implies for Alumbra's `AdManager` and timeline model (D-25, D-26).

## 1. Definitions

DASH-IF IOP Part 5 ([v5.0.0, §4.2](https://dash-industry-forum.github.io/docs/IOP-Guidelines/DASH-IF-IOP-Part5-v5.0.0.pdf))
gives the vendor-neutral definitions:

| Model | DASH-IF definition | Who stitches | Player sees |
|-------|--------------------|--------------|-------------|
| CSAI | Ad opportunities "described and loaded external from the main player in a secondary player" (out of scope for IOP-5) | Client, separate player | Two media sessions |
| SSAI | "The ad is inserted in the network before reaching the DASH Client" | Server or CDN (manifest manipulator or stitcher) | One continuous stream, with ads as segments, Periods or discontinuities |
| SGAI | "Information about ad placement and resolution is inserted in the network, but final resolution is done on demand by the DASH client" | Client, guided by markers in the manifest | Primary stream plus ad assets resolved at play time |

Google's DAI vocabulary maps onto these models:

- **Full service DAI.** [Full service](https://developers.google.com/ad-manager/dynamic-ad-insertion/full-service) is
  "Google's end-to-end implementation of server-side ad insertion (SSAI)" for HLS or DASH. It is used through the IMA
  DAI SDK (HTML5, Android, iOS, tvOS, Chromecast, Roku) or the DAI API.
- **Pod Serving.** [Pod Serving](https://developers.google.com/ad-manager/dynamic-ad-insertion/pod-serving) "lets you
  request ad pods, from Google or third-party ad servers, for live and VOD". The publisher's own stitcher or manifest
  manipulator does the stitching. For live there are two modes:
  - "Pod serving redirect": segment redirect URLs.
  - "Pod serving manifest": complete pod manifests.
- **Server-guided DAI.** [SGAI](https://developers.google.com/ad-manager/dynamic-ad-insertion/server-guided)
  "leverages Pod Serving DAI to provide on-device stitching". Google says it can be implemented "on platforms with
  and without IMA SDK support".
  - Google's
    [announcement](https://ads-developers.googleblog.com/2025/04/unlock-premium-ad-experiences-for.html) (2025-04-07)
    positions SGAI for formats such as squeezebacks and L-banners.
  - The same announcement says Google is designing SGAI "to work with standards, such as HLS interstitials and the
    upcoming DASH specification".

## 2. How Google SGAI works

The [SGAI overview](https://developers.google.com/ad-manager/dynamic-ad-insertion/server-guided) and the
[livestream get-started guide](https://developers.google.com/ad-manager/dynamic-ad-insertion/api/pod-serving/live/sgai-get-started)
describe this flow:

1. **Register a stream session.** `POST https://dai.google.com/ssai/pods/api/v1/network/{network}/custom_asset/{asset}/stream`
   returns:
   - `stream_id`;
   - `pod_manifest_url`, a template containing `$pod-id$`;
   - `metadata_url` and `polling_frequency`;
   - `media_verification_url` and `session_update_url`.
2. **Schedule the break.** The client "schedules when to request pre-conditioned ads", either from metadata polling or
   from ad markers that a stream server (manifest manipulator) optionally inserts in the content manifest. The break
   parameters (`ad_break_id`, duration, `cust_params`, `scte35`) must be identical for all clients. Google recommends,
   but does not require, calling the Early Ad Break Notification (EABN) API about a minute or more before each break.
3. **Fetch the pod at the break.** The client builds the pod manifest URL (for example
   `.../hls/network/.../custom_asset/.../ad_break_id/ab101.m3u8?stream_id=...&pd=120000`), "switches from the content
   stream manifest to play the ad pod manifest", and then switches back.
4. **Report.** The client polls metadata, handles ID3 timed metadata and media verification, and sends tracking pings.
   Pings must not be delayed by more than one minute.

The SDK and platform status of SGAI:

| Platform | IMA DAI SDK status | Player primitive |
|----------|--------------------|------------------|
| HTML5 | Release [3.689.6](https://developers.google.com/ad-manager/dynamic-ad-insertion/sdk/html5/history) (2025-03-24) "Fixes an issue for server guided ad insertion (SGAI)" with delayed `AD_BREAK_STARTED`/`AD_PERIOD_STARTED` events | hls.js [v1.6.0](https://github.com/video-dev/hls.js/releases/tag/v1.6.0) added HLS Interstitials (`enableInterstitialPlayback`, `hls.interstitialsManager`, see [API.md](https://github.com/video-dev/hls.js/blob/master/docs/API.md)) |
| iOS | [3.26.1](https://developers.google.com/ad-manager/dynamic-ad-insertion/sdk/ios/history) (2025-04-02) handles HLS interstitials metadata. 3.28.10 (2025-10-22) "Enables interstitial support for Server-Directed Ad Insertion (SDAI) live and VOD streams" and raises the minimum to iOS 15. 3.32.0 (2026-05-29) adds skippable ads with interstitials | AVFoundation `AVPlayerInterstitialEvent` (iOS 15+) |
| tvOS | [4.15.1](https://developers.google.com/ad-manager/dynamic-ad-insertion/sdk/tvos/history) (2025-05-07), 4.16.0 (2025-10-23) and 4.17.0 (2026-05-29) mirror the iOS releases | Same as iOS |
| Android | No SGAI or interstitial entries in the [Android DAI release notes](https://developers.google.com/ad-manager/dynamic-ad-insertion/sdk/android/history) | Media3 `HlsInterstitialsAdsLoader` (see below) |

On iOS and tvOS the switch is the `useHLSInterstitials` property of
[`IMALiveStreamRequest`](https://developers.google.com/ad-manager/dynamic-ad-insertion/sdk/ios/reference/Classes/IMALiveStreamRequest)
and `IMAVODStreamRequest`. The release notes spell it `useHLSInterstitial`. The property "will request that DAI use HLS
interstitials, or return an error if interstitials are not supported". The SDK checks device compatibility only when
`IMAAVPlayerVideoDisplay` is used, not with a custom video display.

Media3 support for these features:

- **Ad insertion.** The [ad insertion guide](https://developer.android.com/media/media3/exoplayer/ad-insertion) lists
  three paths:
  - `ImaAdsLoader` for CSAI;
  - `ImaServerSideAdInsertionMediaSource` for IMA DAI SSAI;
  - `HlsInterstitialsAdsLoader` for "server-guided client-side ad insertion".
- **HLS.** The [HLS feature table](https://developer.android.com/media/media3/exoplayer/hls) marks HLS Interstitials as
  "Partially — Only VOD with X-ASSET-URI", with live and `X-ASSET-LIST` to come later, and SCTE-35 as unsupported.
- **DASH.** The [DASH table](https://developer.android.com/media/media3/exoplayer/dash) lists EMSG metadata and
  multi-period as supported, but "Server-guided ad insertion (xlinks)" as unsupported.

## 3. HLS Interstitials

HLS Interstitials are defined in Appendix D of
[draft-pantos-hls-rfc8216bis-22](https://datatracker.ietf.org/doc/html/draft-pantos-hls-rfc8216bis-22) (2026-05-01,
protocol version 13). The server schedules an interstitial with an `EXT-X-DATERANGE` tag that has
`CLASS="com.apple.hls.interstitial"`. Interstitials "MUST be VOD assets" but can be scheduled into VOD, live or LL-HLS
primary content, and the client should load them late, after buffering up to the scheduled time.

| Attribute | Meaning |
|-----------|---------|
| `X-ASSET-URI` | A single interstitial asset (a Playlist). Either this or `X-ASSET-LIST` is required |
| `X-ASSET-LIST` | URI to JSON `{"ASSETS": [{"URI", "DURATION"}], "SKIP-CONTROL"?}`, played back to back |
| `X-RESUME-OFFSET` | Where primary playback resumes, relative to the scheduled point. Typical value 0 (insert). If absent, it equals the interstitial duration (replace, appropriate for live) |
| `X-PLAYOUT-LIMIT` | Upper bound on interstitial playout time |
| `X-SNAP` | `OUT` and/or `IN`: snap the transition to the nearest primary segment boundary |
| `X-RESTRICT` | `SKIP` (no seek forward or rate above 1 during the ad) and `JUMP` (no seeking past the break without playing it) |
| `CUE` | `PRE`, `POST`, `ONCE` (pre-roll, post-roll, play once) |
| `X-TIMELINE-OCCUPIES` / `X-TIMELINE-STYLE` | UI hints: `POINT`/`RANGE`, `HIGHLIGHT`/`PRIMARY` |
| `X-CONTENT-MAY-VARY` | Whether the interstitial content may differ between clients |
| `X-SKIP-CONTROL-OFFSET` / `-DURATION` / `-LABEL-ID` | Skip button timing and localised label |

Further rules in the draft:

- **Nesting.** Interstitials nested inside interstitials "MUST be ignored".
- **Seeking and errors (§D.5).** If the interstitial has a non-zero resume offset and the user seeks between its
  start and its resumption point, the client SHOULD play it from the start. If the asset or asset-list request fails,
  the client SHOULD cancel the interstitial with a resume offset of 0; a failing asset inside a list is skipped.
- **Preloading.** Appendix F adds `CLASS="com.apple.hls.preload"` date ranges (`X-URI`, `X-TARGET-ID`,
  `X-TARGET-CLASS`) so clients can preload an asset list at a random point in a window.
- **Personalisation.** The query parameters `_HLS_interstitial_id`, `_HLS_primary_id` and `_HLS_start_offset` let the
  server personalise asset responses.
- **SCTE-35.** Section 4.4.5.1.1 carries SCTE-35 in `EXT-X-DATERANGE` through `SCTE35-CMD`, `SCTE35-OUT` and
  `SCTE35-IN`. That mapping is separate from the interstitial class.

Example from the draft (§D.6): a 15-second ad four seconds into a six-second asset, with no skipping:

```
#EXT-X-DATERANGE:ID="ad1",CLASS="com.apple.hls.interstitial",
    START-DATE="2020-01-02T21:55:44.000Z",DURATION=15.0,
    X-ASSET-URI="http://example.com/ad1.m3u8",X-RESUME-OFFSET=0,
    X-RESTRICT="SKIP,JUMP",X-COM-EXAMPLE-BEACON=123
```

AVFoundation implements interstitials natively:

- **Events.** [`AVPlayerInterstitialEvent`](https://developer.apple.com/documentation/avfoundation/avplayerinterstitialevent)
  (iOS 15+) exposes properties that mirror the tag attributes:
  - `resumptionOffset`, `playoutLimit`, `restrictions`, `cue`;
  - `timelineOccupancy`, `skipControlTimeRange`, `assetListResponse`, `userDefinedAttributes`.
- **Controller.**
  [`AVPlayerInterstitialEventController`](https://developer.apple.com/documentation/avfoundation/avplayerinterstitialeventcontroller)
  lets an app schedule events programmatically and provides `cancelCurrentEvent(withResumptionOffset:)` and
  `skipCurrentEvent()`. Setting `events` makes the player ignore interstitials declared in the playlist.
- **Monitor.**
  [`AVPlayerInterstitialEventMonitor`](https://developer.apple.com/documentation/avfoundation/avplayerinterstitialeventmonitor)
  exposes `currentEvent` and the separate `interstitialPlayer`. The primary player waits with reason
  `.interstitialEvent` while an ad plays.

## 4. DASH equivalents

| Mechanism | Signalling | Use |
|-----------|------------|-----|
| MPD `EventStream` with SCTE-35 | `urn:scte:scte35:2013:xml`, `urn:scte:scte35:2014:xml+bin` (SCTE 214-1, per IOP-5 §5.5.2) | Splice markers in the manifest, for SSAI or SGAI decisions |
| Inband `emsg` with SCTE-35 | `urn:scte:scte35:2013:bin` (SCTE 214, used by [livesim2](https://github.com/Dash-Industry-Forum/livesim2/blob/main/pkg/scte35/scte35.go)) | Splice markers carried in segments |
| Multi-Period | New `Period` per ad (IOP-5) | The common SSAI form in DASH |
| XLink remote Period | `@xlink:href` with `@xlink:actuate="onRequest"` (IOP-5 §5.9.4) | SGAI placeholder resolved by the client. IOP-5 says remote elements "must be further studied", especially in dynamic MPDs |
| Callback event | `urn:mpeg:dash:event:callback:2015` (IOP-5 §5.8.2) | Tracking beacons |
| Alternative MPD events | `InsertPresentation` / `ReplacePresentation` in ISO/IEC 23009-1 6th edition ([MPEG schema](https://github.com/MPEGGroup/DASHSchema), branch `6th-Ed`) | The DASH counterpart of HLS Interstitials |

IOP-5 calls a SCTE-35 `time_signal()` carrying a `segmentation_descriptor()` the common practice, with the "legacy"
`splice_insert()` as the alternative. It also uses the event `@id` for de-duplication.

Alternative MPD details:

- **Attributes.** The event schema (`AlternativeMPDEventType`) carries `uri`, `earliestResolutionTimeOffset`,
  `maxDuration`, `executeOnce`, `noJump`, `skipAfter` and `serviceDescriptionId`. Replace events add `returnOffset`,
  `clip` and `startWithOffset` (attribute names per the MPEG schema and [Eyevinn dash-mpd](https://github.com/Eyevinn/dash-mpd)).
- **Replace scheme and behaviour.** The replace scheme is `urn:mpeg:dash:event:alternativeMPD:replace:2025`, as used by
  [livesim2's SGAI mode](https://github.com/Dash-Industry-Forum/livesim2/blob/main/cmd/livesim2/app/sgai.go). Per
  livesim2, "A player resolves that URI between earliestResolutionTime and presentationTime, switches to the returned
  (List) MPD for the break, and then resumes the live presentation."
- **Insert scheme and publication.** The insert-scheme URI and the publication status of the 6th edition are
  (unverified).
- **Player support.** The [dash.js feature table](https://github.com/Dash-Industry-Forum/dash.js/blob/development/docs/site/pages/features/index_features.md)
  lists Server Guided Ad Insertion as "Not supported" in dash.js. livesim2 supports "DASH 6th edition Alternative-MPD
  Replace events". castLabs has an [overview article](https://castlabs.com/blog/alternative-mpd-events/).

## 5. Stream time vs content time

With SSAI, ads occupy real time on the media timeline, so three clocks diverge:

- **Stream time.** The position in the stitched stream, which is what `<video>.currentTime` reports.
- **Content time.** The position in the content without ads, which bookmarks, the content seek bar, analytics and
  resume use.
- **Ad time.** The position within the current ad or pod.

The IMA HTML5 DAI
[`StreamManager`](https://developers.google.com/ad-manager/dynamic-ad-insertion/sdk/html5/reference/js/StreamManager)
handles the conversion:

- `contentTimeForStreamTime()` and `streamTimeForContentTime()` convert between the clocks. For live streams the
  former "Returns the given stream time".
- `previousCuePointForStreamTime()` is for snapback: forcing the viewer to watch an ad they seeked past.
- `processMetadata()` and `onTimedMetadata()` feed it ID3 metadata.

SGAI and interstitials change the picture:

- **Primary timeline unchanged.** The primary timeline never contains the ad, so content time equals primary stream
  time. AVFoundation plays the ad on a separate `interstitialPlayer`. hls.js can either use a separate asset player or,
  with `interstitialAppendInPlace` (default `true`), append the ad "on a single timeline, with the same SourceBuffers
  and MediaSource as the primary media" when the schedule aligns with primary media
  ([API.md](https://github.com/video-dev/hls.js/blob/master/docs/API.md)).
- **Resume offset.** `X-RESUME-OFFSET` (HLS) and `returnOffset` (DASH replace) decide whether the break consumes
  primary time: replace, as in live, or insert, as in VOD with offset 0.
- **Restrictions.** `X-RESTRICT` and `noJump` move snapback rules into the manifest, where the player must enforce them.

## 6. Implications for Alumbra

- **One `AdBreak` model, several providers.** Umbra's `AdManager` (ADR 0008) should normalise every signal (VMAP;
  SCTE-35 from DATERANGE, `EventStream` or `emsg`; HLS interstitial date ranges; DASH Alternative MPD events; IMA DAI
  cue points) into `AdBreak { id, scheduledTime, duration, resumeOffset, restrictions, assets[] }`.
- **Two playback strategies.** Stitched (SSAI) uses one pipeline with ad ranges marked on the timeline. Interstitial
  (SGAI) uses either a second pipeline or in-place appending into the same Penumbra `SbPlayer`-like contract, as hls.js
  does with MSE. Desktop needs Umbra's own interstitial scheduler. On iOS and tvOS, Alumbra can delegate to
  AVFoundation when it uses `AVPlayer`.
- **Timeline API.** Expose content time as the public `currentTime`, with stream time as an explicit secondary
  property. Alternatively, mirror HTMLMediaElement strictly and add mapping helpers like IMA's. This is the core of
  D-26.
- **Enforcement in Umbra.** Implement the `SKIP`/`JUMP` (`noJump`) restrictions, snap rules and pre/post/once cues in
  the engine, not in the UI, so every Antumbra surface behaves the same way.
- **IMA bridge surface (D-25).** IMA providers need timed metadata (ID3 and `emsg`), stream time, seek interception
  for snapback and the ad container view from Umbra, and return cue points and ad events. The pod-serving and SGAI
  APIs work without an IMA SDK, so the desktop VAST/VMAP provider can later grow a direct Pod Serving client
  (unverified feasibility for production use).

## 7. Decisions to make

- **D-25: IMA ↔ Umbra surface.** Which events and time queries cross the Penumbra boundary; whether IMA DAI owns the
  video element or player (web `StreamManager`, `IMAAVPlayerVideoDisplay`) or receives a custom video display backed
  by Umbra; and how the ad UI layering from D-23 applies.
- **D-26: DAI and SGAI timeline model.** The public time semantics (content vs stream time); ad-range representation
  on the seek bar (`X-TIMELINE-OCCUPIES`/`-STYLE`); the snapback policy; and whether SGAI uses a second decoder
  pipeline or in-place appending on one pipeline. Support targets: HLS Interstitials first, with DASH Alternative MPD
  after dash.js or other players adopt it.

## References

- DASH-IF. *IOP Guidelines Part 5: Ad Insertion, v5.0.0*. https://dash-industry-forum.github.io/docs/IOP-Guidelines/DASH-IF-IOP-Part5-v5.0.0.pdf
- Google. *Full service DAI*. https://developers.google.com/ad-manager/dynamic-ad-insertion/full-service
- Google. *Pod Serving DAI*. https://developers.google.com/ad-manager/dynamic-ad-insertion/pod-serving
- Google. *Server-guided DAI*. https://developers.google.com/ad-manager/dynamic-ad-insertion/server-guided
- Google. *SGAI livestream get started*. https://developers.google.com/ad-manager/dynamic-ad-insertion/api/pod-serving/live/sgai-get-started
- Google Ads Developer Blog. *Unlock premium ad experiences...* (2025-04-07). https://ads-developers.googleblog.com/2025/04/unlock-premium-ad-experiences-for.html
- Google. *IMA DAI HTML5 StreamManager*. https://developers.google.com/ad-manager/dynamic-ad-insertion/sdk/html5/reference/js/StreamManager
- Google. *IMA DAI HTML5 release history*. https://developers.google.com/ad-manager/dynamic-ad-insertion/sdk/html5/history
- Google. *IMA DAI iOS release history*. https://developers.google.com/ad-manager/dynamic-ad-insertion/sdk/ios/history
- Google. *IMA DAI tvOS release history*. https://developers.google.com/ad-manager/dynamic-ad-insertion/sdk/tvos/history
- Google. *IMA DAI Android release history*. https://developers.google.com/ad-manager/dynamic-ad-insertion/sdk/android/history
- Google. *IMALiveStreamRequest (iOS)*. https://developers.google.com/ad-manager/dynamic-ad-insertion/sdk/ios/reference/Classes/IMALiveStreamRequest
- Android Developers. *Media3 ad insertion*. https://developer.android.com/media/media3/exoplayer/ad-insertion
- Android Developers. *Media3 HLS*. https://developer.android.com/media/media3/exoplayer/hls
- Android Developers. *Media3 DASH*. https://developer.android.com/media/media3/exoplayer/dash
- R. Pantos. *HTTP Live Streaming 2nd Edition, draft-pantos-hls-rfc8216bis-22*. https://datatracker.ietf.org/doc/html/draft-pantos-hls-rfc8216bis-22
- Apple. *AVPlayerInterstitialEvent*. https://developer.apple.com/documentation/avfoundation/avplayerinterstitialevent
- Apple. *AVPlayerInterstitialEventController*. https://developer.apple.com/documentation/avfoundation/avplayerinterstitialeventcontroller
- Apple. *AVPlayerInterstitialEventMonitor*. https://developer.apple.com/documentation/avfoundation/avplayerinterstitialeventmonitor
- hls.js. *v1.6.0 release*. https://github.com/video-dev/hls.js/releases/tag/v1.6.0
- hls.js. *API.md*. https://github.com/video-dev/hls.js/blob/master/docs/API.md
- MPEG. *DASHSchema (6th edition)*. https://github.com/MPEGGroup/DASHSchema
- DASH-IF. *livesim2 SGAI mode*. https://github.com/Dash-Industry-Forum/livesim2/blob/main/cmd/livesim2/app/sgai.go
- DASH-IF. *livesim2 SCTE-35*. https://github.com/Dash-Industry-Forum/livesim2/blob/main/pkg/scte35/scte35.go
- DASH-IF. *dash.js feature table*. https://github.com/Dash-Industry-Forum/dash.js/blob/development/docs/site/pages/features/index_features.md
- Eyevinn. *dash-mpd*. https://github.com/Eyevinn/dash-mpd
- castLabs. *Alternative MPD events*. https://castlabs.com/blog/alternative-mpd-events/
