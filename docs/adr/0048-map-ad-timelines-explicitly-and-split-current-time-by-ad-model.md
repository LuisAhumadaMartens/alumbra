# 0048 — Map ad timelines explicitly and split `currentTime` by ad model

- **Status:** Accepted
- **Date:** 2026-11-01
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #26

## Context

D-26 decides how Umbra handles ads that are stitched into the stream (SSAI) or signalled for insertion by the
client (SGAI): how it maps stream, presentation and content time, how it plays interstitials and what the public
element reports. The visible effects are the seek bar, `currentTime` and the transitions at ad breaks.
[ADR-0008](0008-ads-architecture.md) already puts the break table and the stream-time to content-time mapping in
`umbra::ads`.

The issue proposed that the public `currentTime` is presentation time. That conflicts with
[ADR-0023](0023-public-api-surface-and-media-error.md), which decided that "during a client-side ad break the
element keeps reporting the content", and that server-side inserted ads are ordinary content whose timeline the ads
module maps, a task it assigned to D-26. The maintainer confirmed the rule below as a public rule, so it is
recorded here as a refinement of ADR-0023. ADR-0023's text is not edited.

Facts the decision rests on, from the definition issue and the research notes:

- **HLS Interstitials** carry `X-ASSET-URI` or `X-ASSET-LIST`, with `X-RESUME-OFFSET`, `X-SNAP`, `X-RESTRICT`,
  `CUE`, `X-PLAYOUT-LIMIT`, `X-TIMELINE-OCCUPIES` and `X-TIMELINE-STYLE`.
- **AVFoundation** schedules interstitials on a separate player and exposes a combined timeline from iOS and tvOS
  18 through `AVPlayerItemIntegratedTimeline`. hls.js 1.6 and later implements interstitials with in-place
  appending to the same `MediaSource`. Media3 lists interstitials as partial.
- **IMA DAI StreamManager** maps stream time to content time on HTML5, Android and iOS and drives snapback with
  `previousCuePointForStreamTime`.
- **DASH** has SCTE-35 events and Alternative MPD insert and replace. dash.js supports SCTE-35 and XLink but not
  Alternative MPD, and DASH-IF livesim2 generates Alternative MPD replace events for testing.

Settled elsewhere and not reopened here:

- **Clock and time types** ([ADR-0024](0024-media-clock-and-av-sync.md)). Umbra derives `currentTime` from
  `GetSnapshot` and never lets it run backwards between seeks.
- **Playhead and snapback** ([ADR-0043](0043-normalise-ads-in-one-ad-manager-with-capability-negotiating-providers.md)).
  The AdManager's playhead reports content time during client-side breaks, and `ads.seek.play_skipped_breaks`
  defaults to `last`.
- **Apple path** ([ADR-0045](0045-render-apple-playback-from-sample-buffers-with-an-avplayer-capability-in-m7.md)).
  The `AVPlayer` capability exists from M7 and can play interstitials natively.
- **Layering** ([ADR-0042](0042-keep-player-chrome-off-the-ad-and-reduce-it-to-an-ad-band.md)).
  Ad-mode controls apply to stitched and guided breaks too.

## Decision drivers

- One public rule per ad model that matches what the viewer sees and what the stream describes.
- Consistency with the content-time rule ADR-0023 set for client-side breaks.
- Restrictions in HLS Interstitials (resume offset, snap, restrict) honoured by shared code, not by each renderer.
- Single-decoder TVs keep working: no second pipeline unless the device allows it.
- Pure, spec-tested time mapping.

## Considered options

1. **A — Single stitched timeline.** Everything is stream time, simple, but it loses content time and makes the
   interstitial rules hard to honour.
2. **B — Multi-timeline model with explicit mappings.** Primary, presentation and UI timelines connected by pure
   functions over an ad-break table.
3. **C — Delegate to the platform where available.** AVPlayer on Apple and Media3's loader on Android, with Umbra
   covering only web and desktop.

What the public `currentTime` reports:

1. **Split by ad model.** Content time for CSAI and SGAI, presentation time for stitched SSAI.
2. **Presentation time everywhere.** Monotonic and matches the screen, but conflicts with ADR-0023 for SGAI.
3. **Content time everywhere.** One rule, but SSAI position stalls through breaks and duration excludes ad time.

## Decision

Chosen option: **B, with in-place playback first**, and **`currentTime` split by ad model**. The split keeps
ADR-0023's rule for ads the client inserts and gives stitched streams the position and duration their manifest
describes.

### What `currentTime` reports

| Ad model | `currentTime` | `duration` and `seekable` |
|---|---|---|
| Client-side (CSAI, VAST and VMAP) | Content time | Content |
| Server-guided (SGAI, HLS Interstitials and the DASH equivalent) | Content time | Content |
| Stitched (SSAI, IMA DAI) | Presentation time | The stitched stream |

- This refines ADR-0023. It makes explicit that SGAI breaks, which the client schedules, follow the client-side
  rule, and that stitched SSAI is ordinary content whose time is presentation time.
- A `contentTime` extension reports content time everywhere, including for stitched streams. Read-only extensions
  expose the current break and the position within it. Their names are fixed in #248 and they are labelled
  `experimental` under ADR-0023's stability rule.
- Apps that need one time base across models read `contentTime`.

### Timelines

Umbra owns the ad-break table and three named timelines: **primary** (the content asset's media time),
**presentation** (what plays, including interstitials) and **UI** (what the seek bar shows). Pure mapping
functions connect them and are unit-tested against the specification's examples. An `AdBreak` carries its id,
scheduled time, duration, resume offset, playout limit, snap, restrictions, cue and assets. For SSAI the table comes
from the DAI provider or timed metadata, and for SGAI from the manifest.

### Restrictions

`X-RESTRICT`, `X-SNAP` and `CUE` are enforced in Umbra, not in renderers. Seeks into restricted ranges are adjusted
and the adjustment is reported.

### Playback

- **In place first.** The first segments of a break are pre-buffered inside the ad budget (D-12). The decoder is
  reconfigured without a flush where codec parameters allow, otherwise flushed in a controlled way.
- A second pipeline is used only where D-04 capabilities say concurrent decoders exist.

### Order of work

1. HLS Interstitials: `X-ASSET-URI` first, then `X-ASSET-LIST`.
2. IMA DAI SSAI, with the table built from StreamManager cue points.
3. DASH Alternative MPD. DASH scope is SCTE-35 `EventStream` plus Alternative MPD insert and replace. XLink is
   excluded; the reasoning for that exclusion is unverified and is reviewed when DASH work starts.

Tests use livesim2 and the specification's examples. Whether Apple publishes sample streams for interstitials is
unverified.

### Seek bar and seek policy

- The seek bar (D-28) uses the UI timeline. `X-TIMELINE-OCCUPIES` and `X-TIMELINE-STYLE` drive the markers, and ad
  markers are shown.
- Seek policy is shared with client-side breaks through `ads.seek.play_skipped_breaks` (default `last`).

### Live

- Breaks are discovered from `EXT-X-DATERANGE` entries in the sliding window (D-13, ADR-0046).
- `X-PLAYOUT-LIMIT` is enforced against the media clock, not wall time.
- Asset-list requests carry `_HLS_primary_id`, `_HLS_interstitial_id` and `_HLS_start_offset` as the HLS
  specification asks. That detail is unverified and is checked against the specification text in #255.

### Events and Apple

Ad events for SGAI come from Umbra's scheduler, or from IMA Pod Serving where present. Apple `AVPlayer` delegation
stays a capability of ADR-0045, not the model.

## Consequences

**Positive**
- Content-time consumers (UI, analytics, cue points) see the same rule for every client-inserted break.
- Stitched streams report the position, duration and seekable range that the stream itself describes.
- Time mapping is pure and spec-tested, and restrictions are enforced once.

**Negative / costs**
- Two public rules to document, and apps must know which ad model they use or read `contentTime`.
- More API surface: `contentTime` and the break accessors.
- The multi-timeline model is the most engineering work of the three options.
- In-place transitions can stall briefly on single-decoder devices.

**Follow-ups**
- #248 builds the stream-time to content-time mapping, the ad-break table, the `contentTime` and break accessors
  and the public `currentTime` rule for each ad model, with tests that assert it per model.
- #249 builds ad-aware seeking, snapback and the timeline UI, including the markers and the shared seek policy.
- #255 parses and schedules HLS interstitials, including `X-ASSET-LIST` and the live rules.
- #257 builds the DASH SGAI equivalent: SCTE-35 and Alternative MPD tested against livesim2.
- A new M10 task adds the public API documentation of the split rule and the `contentTime` extension to the
  ADR-0023 member table, and a conformance case per ad model.
- ADR-0023 is unchanged. This ADR refines it, and the member table generated from the single source file carries the
  new extensions.
- Settings: no new key. `ads.seek.play_skipped_breaks` is added by ADR-0043.

## Pros and cons of the options

### A — Single stitched timeline
- Good, because the public API is the simplest.
- Bad, because content time is lost, and the resume, snap and occupies rules become hard to honour.

### B — Multi-timeline model
- Good, because it has full interstitial fidelity, a testable pure mapping and one model for HLS and DASH.
- Bad, because it adds API surface and engineering cost.

### C — Platform delegation
- Good, because it is the least code on Apple.
- Bad, because behaviour differs per platform, Umbra's ABR, metrics and ad events are bypassed during breaks and
  there is no DASH.

### What `currentTime` reports
- Split by ad model is good for consistency with ADR-0023 and for stitched seek bars, and bad because it has two
  rules.
- Presentation time everywhere is good because it is monotonic and matches the screen, and bad because it conflicts
  with ADR-0023 for SGAI.
- Content time everywhere is good for one rule, and bad because SSAI position stalls through breaks and duration
  excludes ad time.

## References

- [IETF: HLS 2nd edition draft (draft-pantos-hls-rfc8216bis), Appendix D](https://datatracker.ietf.org/doc/draft-pantos-hls-rfc8216bis/)
- [Apple: AVPlayerInterstitialEventController](https://developer.apple.com/documentation/avfoundation/avplayerinterstitialeventcontroller)
- [Apple: AVPlayerItemIntegratedTimeline](https://developer.apple.com/documentation/avfoundation/avplayeritemintegratedtimeline)
- [IMA DAI HTML5: StreamManager](https://developers.google.com/ad-manager/dynamic-ad-insertion/sdk/html5/reference/js/StreamManager)
- [IMA DAI iOS: IMALiveStreamRequest](https://developers.google.com/ad-manager/dynamic-ad-insertion/sdk/ios/reference/Classes/IMALiveStreamRequest)
- [hls.js: API.md](https://github.com/video-dev/hls.js/blob/master/docs/API.md)
- [Media3: HLS](https://developer.android.com/media/media3/exoplayer/hls)
- [DASH-IF: Interoperability Points](https://dashif.org/guidelines/)
- [DASH-IF: livesim2](https://github.com/Dash-Industry-Forum/livesim2)
- [dash.js](https://github.com/Dash-Industry-Forum/dash.js)
- [ADR-0008](0008-ads-architecture.md), [ADR-0023](0023-public-api-surface-and-media-error.md),
  [ADR-0024](0024-media-clock-and-av-sync.md)
- [Research: SSAI and SGAI](../research/ssai-and-sgai.md)
