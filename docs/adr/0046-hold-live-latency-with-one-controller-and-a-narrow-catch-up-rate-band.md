# 0046 — Hold live latency with one controller and a narrow catch-up rate band

- **Status:** Accepted
- **Date:** 2026-11-01
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #13

## Context

D-13 decides how Umbra models the live edge, the target latency, the DVR window and catch-up, so that HLS, LL-HLS,
DASH and LL-DASH share one live controller. It blocks #218, #219 and #220 and shapes the LL-HLS and LL-DASH work.
Catch-up is audible and visible, so it decides how live playback feels.

The two protocols describe the same concepts with different signals:

- **Live edge.** HLS uses the end of the last listed segment or part. DASH uses the latest segment whose
  availability time has passed, and `availabilityTimeOffset` makes chunks available early.
- **Default distance from the edge.** HLS has `HOLD-BACK` (at least three target durations), `PART-HOLD-BACK` in
  low latency and `EXT-X-START`. DASH has `suggestedPresentationDelay` and `ServiceDescription` with `Latency`
  (`target`, `min`, `max`) and `PlaybackRate` (`min`, `max`).
- **DVR window.** HLS uses the sliding playlist. DASH uses `timeShiftBufferDepth`.
- **Clock.** HLS has no clock synchronisation in the specification, so the client clock is trusted. DASH has
  `UTCTiming`. TVs and set-top boxes often boot with a wrong clock, so latency computed against a skewed clock makes
  a controller chase a phantom target.

Settled elsewhere and not reopened here:

- **Rates** ([ADR-0024](0024-media-clock-and-av-sync.md)). Playback rates from 0.25 to 4 are supported with pitch
  preserved, and the media clock carries a resample ratio next to the rate.
- **Port contract** ([ADR-0016](0016-penumbra-port-contract.md)). Ports declare what they support, and
  `SetPlaybackRate` accepts finite, non-negative rates.
- **Settings** ([ADR-0017](0017-settings-schema-and-configuration.md)). A unit is the key's last suffix, and `live`
  keys are the ones a per-source override may change.
- **Errors** ([ADR-0018](0018-error-model-and-recovery.md)). Recoverable conditions are reported as degraded or
  recoverable, never as fatal.
- **Presentation model** (D-10, #10), **buffering** (D-12, #12) and **ABR** (D-11, #11) supply the timeline, the
  back buffer and the throughput estimate.

## Decision drivers

- Playback stays close to the live edge without anyone hearing the correction.
- One implementation of every policy, testable on simulated time.
- Behaviour is the same for HLS and DASH, within what each manifest signals.
- Playback never fails because a port lacks rate control or a server is not fully low-latency.
- The app and the user can always override the manifest.

## Considered options

1. **A — Per-protocol live logic.** Faithful to each specification, but every policy is written and tested twice.
2. **B — Unified controller over a protocol-neutral live timeline.** One controller adjusts the rate to hold the
   target, with seek-based catch-up as the fallback.
3. **C — Seek-only latency management.** No rate changes, and a visible jump when latency exceeds the limit.

How the controller closes latency by default:

1. **Narrow rate band.** 0.97 to 1.03 with hysteresis, seeking to live beyond the maximum.
2. **Seek only.** No rate change, perfect cadence, a visible jump.
3. **Wide band.** 0.9 to 1.1, which catches up quickly with clearly noticeable speed changes.

## Decision

Chosen option: **B, with the narrow rate band as the default catch-up** and seek-to-live as the fallback. A narrow
band is hard to hear, and one controller keeps catch-up behaviour identical for both protocols.

### Live timeline

The presentation model exposes an availability window, a live-edge estimate, recommended target, minimum and
maximum latency, rate bounds and a wall-clock mapping. Both protocol parsers fill it, so the controller never sees a
protocol.

### Target precedence

1. The app or user setting.
2. The manifest: `ServiceDescription`, `PART-HOLD-BACK`, `HOLD-BACK` or `suggestedPresentationDelay`.
3. Three target durations.

The manifest may narrow the app's catch-up band but never widen it.

### Controller

- A proportional controller with hysteresis. The target grows after each rebuffer, up to the maximum.
- A PI controller is added only if the simulator in #122 shows steady-state error.
- The rate band is 0.97 to 1.03 by default, with pitch preserved (ADR-0024).
- When the latency exceeds the maximum, or the port reports no rate support, the controller seeks to live. Seek to
  live is also an explicit API call.
- The port declares its supported rate range and step. The Safari rate-granularity quirk is handled in the web port;
  it is unverified.

### Clock

- DASH uses `UTCTiming`.
- HLS measures latency against the playlist's live edge. `EXT-X-PROGRAM-DATE-TIME` skew is a diagnostic only, until
  it is shown to be reliable enough to drive catch-up.

### Behind the window and low latency

- Playback that falls behind the window resumes at the window start plus a margin, with a recoverable error (D-09).
- Low latency is a mode, not a separate player. It degrades to standard latency when the server profile is
  incomplete, reported as degraded (ADR-0018).

### API

In the extension namespace and labelled `experimental`: `liveLatency`, `targetLatency`, `seekToLive()` and
`isAtLiveEdge`. DVR seeks use the manifest window, and the back buffer stays small (D-12). Ads in sliding windows
belong to D-26.

### Settings

The issue's proposed keys are adopted with ADR-0017 units. Rates have no unit suffix in the issue, so they become
percentages of normal speed.

| Key | Meaning |
|---|---|
| `live.target_latency_ms` | Target distance from the live edge |
| `live.min_latency_ms`, `live.max_latency_ms` | Latency band; beyond the maximum the controller seeks |
| `live.catchup.min_rate_percent` | Lower rate bound, default 97 |
| `live.catchup.max_rate_percent` | Upper rate bound, default 103 |
| `live.low_latency.enabled` | Allows low-latency mode when the manifest qualifies |

### Cadence test

At 24 fps with display resample, a rate other than 1 may cause occasional dropped or repeated frames. That claim is
unverified, so the controller is tested against ADR-0024's cadence work at 24, 23.976 and 25 fps on 60 Hz, 120 Hz
and 144 Hz.

## Consequences

**Positive**
- One live controller, one set of tests and one behaviour for HLS, LL-HLS, DASH and LL-DASH.
- Catch-up is hard to hear, and playback degrades to a seek instead of failing.
- Apps and users can override the manifest, and the clock-skew risk is contained.

**Negative / costs**
- The timeline must expose every signal in the table above, so the parsers carry more work.
- Narrow catch-up is slow, so a large latency error ends in a seek.
- Rate changes may interact with cadence selection at some content and display rates.
- A rate-capable port is needed for gentle catch-up, and ports differ in granularity.

**Follow-ups**
- #218 implements the live edge, the DVR window and seek-to-live, including the timeline and the clock sources.
- #219 implements the target latency and the playback-rate catch-up, with the controller, hysteresis, the
  rebuffer-driven target growth and the cadence test against ADR-0024's work.
- #220 implements low-latency bandwidth estimation, with the degrade path to standard latency.
- #122 (the ABR simulator) evaluates whether a PI controller is needed.
- LL-HLS (#221) and LL-DASH (#225) feed the live timeline.
- A new task adds the `live.*` keys above to the settings schema and notes that `live` keys are
  overridable per source.
- A new task measures whether HLS `EXT-X-PROGRAM-DATE-TIME` skew becomes reliable enough to drive catch-up.
- Live ads in sliding windows are decided in D-26 (#26, ADR-0048).

## Pros and cons of the options

### A — Per-protocol logic
- Good, because each protocol keeps full fidelity to its specification.
- Bad, because every policy is written and tested twice.

### B — Unified controller
- Good, because behaviour is consistent, simulation is simple and it matches Media3's shape.
- Bad, because the timeline must expose every signal and a fallback is needed without rate support.

### C — Seek only
- Good, because it works on every port, with perfect cadence.
- Bad, because the viewer sees jumps and there is no gentle drift.

### Narrow band, seek only or wide band
- Narrow is good because it is hard to hear, and bad because it may disturb cadence at some rates.
- Seek only is good for cadence, and bad for visible jumps.
- Wide is good for fast catch-up, and bad because speed and cadence changes are clearly noticeable.

## References

- [IETF: HTTP Live Streaming 2nd Edition (draft-pantos-hls-rfc8216bis)](https://datatracker.ietf.org/doc/draft-pantos-hls-rfc8216bis/)
- [Apple: Enabling Low-Latency HTTP Live Streaming](https://developer.apple.com/documentation/http-live-streaming/enabling-low-latency-http-live-streaming-hls)
- [DASH-IF: Low-latency Modes for DASH (L3D)](https://dashif.org/DASH-IF-IOP/l3d/)
- [DASH-IF: Timing Model guidelines](https://dashif.org/Guidelines-TimingModel/)
- [MDN: HTMLMediaElement.preservesPitch](https://developer.mozilla.org/en-US/docs/Web/API/HTMLMediaElement/preservesPitch)
- [Media3: DefaultLivePlaybackSpeedControl](https://developer.android.com/reference/androidx/media3/exoplayer/DefaultLivePlaybackSpeedControl)
- [Media3: MediaItem.LiveConfiguration](https://developer.android.com/reference/androidx/media3/common/MediaItem.LiveConfiguration)
- [dash.js: CatchupController.js](https://github.com/Dash-Industry-Forum/dash.js/blob/development/src/streaming/controllers/CatchupController.js)
- [hls.js: API.md](https://github.com/video-dev/hls.js/blob/master/docs/API.md)
- [Shaka Player: player_configuration.js](https://github.com/shaka-project/shaka-player/blob/main/lib/util/player_configuration.js)
- [Apple: configuredTimeOffsetFromLive](https://developer.apple.com/documentation/avfoundation/avplayeritem/configuredtimeoffsetfromlive)
- [ADR-0016](0016-penumbra-port-contract.md), [ADR-0017](0017-settings-schema-and-configuration.md),
  [ADR-0018](0018-error-model-and-recovery.md), [ADR-0024](0024-media-clock-and-av-sync.md)
- [Research: Low latency and protocols](../research/low-latency-and-protocols.md)
