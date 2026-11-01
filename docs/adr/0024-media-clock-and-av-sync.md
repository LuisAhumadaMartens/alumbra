# 0024 — Drive media time from a smoothed audio clock, pick frames by cadence and offer display resample

- **Status:** Accepted
- **Date:** 2026-11-01
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #2

## Context

D-02 decides which clock defines `currentTime`, how video frames are chosen against it, what timing each port must
report and what "in sync" means. It largely decides how playback feels: judder, dropped frames, lip-sync and
smoothness on 120 Hz and 144 Hz displays. The maintainer's standing priority is exactly that: playback that feels
smooth, including at refresh rates above 60 Hz. D-02 blocks #90, and
[ADR-0021](0021-testing-strategy.md#golden-output-and-actual-av-sync) leaves the signed offset, the tolerance and
the measurement conditions to it, so #93 cannot assert a result until they are set.

[ADR-0016](0016-penumbra-port-contract.md#the-reference-player-b-inside-d) places sync, frame dropping and
statistics in Penumbra's shared reference player, which the desktop and WebCodecs ports build from components.
Media Source Extensions, Apple's `AVSampleBuffer*` renderers and tunnelled Android output synchronise inside the
platform and only report media time. This decision therefore designs one clock for the reference player and sets
what every port reports.

Four forces shape it:

- **Clock drift.** The audio DAC runs on its own crystal. A 50 ppm mismatch against the system monotonic clock adds
  up to 180 ms per hour. Audio cannot be dropped or repeated without audible damage; video frames can.
- **Audio latency.** The time between handing audio to the device and hearing it varies by platform and route.
  Bluetooth commonly adds 100–300 ms. SDL3 gives the desktop no presentation timestamp, so the position must be
  estimated from the audio still queued and the device's buffer size.
- **Cadence.** Content frame rates rarely divide the display's refresh rate. 24 fps on 60 Hz needs a 2:3 pattern.
  23.976 fps on 120 Hz needs five refreshes per frame plus one extra refresh about every 8.3 s, because the rates
  differ by 0.1%. A refresh lasts 16.7 ms at 60 Hz, 8.3 ms at 120 Hz, 6.9 ms at 144 Hz and 4.2 ms at 240 Hz, so a
  few milliseconds of clock jitter is a large fraction of a refresh at high rates.
- **Display timing on desktop.** SDL3's GPU API (`SDL_gpu.h` at the time of this decision) offers present modes
  (`VSYNC`, `IMMEDIATE`, `MAILBOX`) and swapchain waits, but no vsync or present timestamps. SDL3 does report each
  display mode's refresh rate as a rational and signals mode and display changes.

Accepted decisions already settle several parts, and this ADR does not reopen them:

- **Time types** ([media types](../architecture/media-types.md), #51). `MediaTime` and `TimeDelta` are `int64_t`
  microseconds, and exact container values stay in `Timestamp`.
- **Port reporting** ([port contract](../architecture/port-contract.md)). `Player::GetSnapshot` returns the media
  clock, frame counters and audio output latency, `AudioSink::GetOutputLatency` reports the sink's latency, the
  platform vsync thread pulls frames with `VideoFrameSource::TakeDueFrame`, and audio reaches the real-time callback
  through lock-free rings ([ADR-0015](0015-threading-and-task-runners.md#paths-outside-the-scheduler)).
- **No frame-rate cap** (ADR-0015). Presentation follows the display's refresh rate, and content frame rates are
  independent of it.
- **Rates** (port contract). `SetPlaybackRate` accepts only finite, non-negative rates, so reverse playback is out.
- **Virtual time** (ADR-0015). Task runners and the media clock read one virtual time source in tests.
- **Measurement** (ADR-0021). Actual sync is measured by capturing flash and beep content, separately from
  deterministic clock tests.

## Decision drivers

- Playback feels smooth: frames follow the most even pattern the frame rate and refresh rate allow, at 60 Hz,
  120 Hz, 144 Hz and above, without micro-stutter.
- Lip-sync stays well inside what people notice, and the claim is measured on real output, not assumed.
- Audio is never dropped, repeated or clicked, and its pitch is untouched by default.
- Every port can meet the reporting requirements; ports that synchronise in the platform keep doing so.
- `currentTime` is steady: it never jitters or runs backwards between seeks.
- Every policy is testable on virtual time with fake sinks and fake vsync clocks.
- The default carries the least risk for M1 (First Light), and users who want the smoothest motion can opt in to
  more.

## Considered options

The definition has seven questions.

The master clock in the reference player:

1. **A — Audio master, display resample later.** Media time follows the audio sink's presented position;
   display resample waits until #93 has measured audio master.
2. **B — Audio master, display resample opt-in from M1.** The same default, plus mpv-style display resample as a
   desktop setting.
3. **C — Display master on desktop.** Display resample is the desktop default; audio master elsewhere.
4. **D — System-clock master.** Media time is an anchor plus monotonic time, and audio is resampled to follow it.

What "in sync" means for #90 and #93:

1. **A — 20 ms steady-state target, failure outside EBU R37** (+40/−60 ms).
2. **B — 10 ms target, failure outside ±20 ms.**
3. **C — ITU-R BT.1359 detectability bounds only** (+45/−125 ms).

What ports report and how the clock is kept steady:

1. **A — Smoothed audio clock, optional presented-frame timestamps.**
2. **B — Presented-frame feedback required from every port.**
3. **C — Raw sink position, unsmoothed.**

Audio route and latency changes during playback:

1. **A — Re-query the latency and slew.**
2. **B — Pause, rebase and resume.**
3. **C — Ignore until the next seek.**

Frame drops, repeats and resync:

1. **A — Per-vsync selection with a cadence model, drop when more than a frame late, skip to a keyframe when far
   behind.**
2. **B — Aggressive dropping:** drop at half a frame late and skip to a keyframe at 200 ms behind.
3. **C — Never skip to keyframes;** only decoded frames are dropped.

`playbackRate` range and clock start:

1. **A — Pitch-preserved 0.25–4×, other rates muted; the clock starts with the first audible sample.**
2. **B — 0.5–2× only,** other rates rejected.
3. **C — Video-led start:** the clock starts with the first video frame and audio joins.

Display refresh-rate switching:

1. **A — D-02 sets the reporting contract; ports request matching rates.**
2. **B — D-02 owns everything,** including the platform calls.
3. **C — Ports only;** the shared code does not know the display rate.

## Decision

Chosen options: **B for the master clock; A for the other six questions, with the frame cadence handled by a
cadence estimator with hysteresis** in the style of Chromium's `VideoRendererAlgorithm` and
`VideoCadenceEstimator`. Audio master is the well-tried default (ffplay, Media3, Chromium). Display resample is
offered from M1 because it is the only way to remove the periodic cadence breaks that near-matching rates cause,
and those breaks are most frequent at high refresh rates. The cadence estimator is what makes frame selection even
at 120 Hz, 144 Hz and above. Type and member names below are illustrative; #90 fixes them.

### Where the clock lives

- The clock, frame selection, drop and resync policy and their statistics are shared code in Penumbra's reference
  player (ADR-0016), not Umbra. The desktop and WebCodecs ports use it.
- Ports that implement the player contract directly (MSE, Apple's `AVSampleBuffer*` path, tunnelled or secure
  Android output) keep their platform's synchronisation and report media time through `GetSnapshot`.
- Umbra reads media time only through `Player::GetSnapshot`. The issue's proposed `MediaTimeProvider` interface is
  therefore not added: `GetSnapshot` already is that interface, for both kinds of port. Umbra derives the public
  `currentTime` from it and never lets it run backwards between seeks.

### Clock sources

- **Audio master.** When the stream has audio, media time follows the position of the sample being heard: the
  media time of the audio handed to the device, minus the output latency the sink reports, minus the user offset
  `playback.sync.audio_offset_ms`.
- **Monotonic fallback.** ADR-0015's monotonic `TickClock` drives media time for video-only streams and for time
  with no audible audio to follow: before the first audio sample when a stream's audio begins later than its video,
  and after audio ends before video. Handover in either direction anchors the new source at the media time already
  reached, so `currentTime` never jumps or runs backwards.
- **One readable state.** The clock publishes its anchor (monotonic time, media time, playback rate and resample
  ratio) atomically, so the vsync thread and the audio callback read it without locks, as ADR-0015 requires of
  real-time paths.
- **Resample ratio.** The clock interface carries a resample ratio next to the playback rate. It is exactly 1 in
  audio master and is set by display resample below. Media time advances at the playback rate times the ratio.

### Smoothing the audio clock

- The reference player's audio source records, for each real-time callback, the media time of the first frame it
  hands over, the number of frames and the monotonic time of the callback. The callback reads only the injected
  clock and atomics; it takes no lock and posts no task.
- The raw audio position at a given moment is derived from the latest callback, minus the latency, advanced by the
  time elapsed since that callback. It is noisy: devices consume audio in bursts of a device buffer, typically
  10–20 ms, and callbacks jitter.
- The media clock is a filtered version of it: an anchor advanced by the monotonic clock at the playback rate, with
  a small correction from a slow control loop that steers it towards the raw position. The correction is bounded to
  a fraction of a percent of the rate, so the clock never jitters or runs backwards and its speed changes are
  imperceptible.
- Route and latency changes slew as described below. Outside them, if the raw position and the clock differ by more
  than the failure bounds of the sync tolerance, the clock is re-anchored instead of steered. A forward correction
  moves at once, and video catches up by dropping frames. A backward correction holds the clock until audio reaches
  it, because `currentTime` never runs backwards.
- Audio underruns stall the audible position, so the clock holds while the sink plays silence. The playback state
  machine (#89) reports the stall.
- Presented-frame timestamps are optional. Where a video sink reports them, they refine drop accounting: a frame
  counts as dropped only if it was never shown, and missed refreshes are detected. They are required only for
  display resample.
- SDL3's queued-bytes estimate, from the audio still queued and the device buffer size (`SDL_GetAudioStreamQueued`,
  `SDL_GetAudioDeviceFormat`), is accepted for the desktop until #93 rules on whether it meets the tolerance. If it
  does not, the desktop audio sink needs native output paths with presentation timestamps, which is D-19's decision.

### Display resample (opt-in on desktop)

- `playback.sync.mode` selects `audio` (the default) or `display_resample`. It is available on Linux, macOS and
  Windows.
- In display resample, video is presented on an exact cadence for the measured refresh interval, and the playback
  speed is adjusted by the resample ratio so the content fits that cadence exactly. For example, 23.976 fps on a
  120 Hz display plays 0.1% faster, five refreshes per frame, with no cadence breaks at all. Audio is resampled by
  the same ratio, so its pitch moves by the ratio: about 1.7 cents in that example. A further correction of at most
  0.125%, mpv's default, keeps the measured A/V offset at zero against drift.
- The ratio is limited by `playback.sync.resample_max_change_percent`, default 1%, matching mpv's
  `--video-sync-max-video-change`. 1% moves pitch by about 17 cents. Content that fits no cadence within the limit
  stays in audio master.
- Media time is still the audio clock, running at the playback rate times the ratio, so `currentTime`, the sync
  measurement and every other consumer keep one definition. `playbackRate` keeps the value the app set.
- Display resample needs real vsync and present timing: the measured refresh interval, because nominal mode rates
  are rounded (59.94 Hz is often reported as 60 Hz), and the actual present time of each frame, to detect missed
  refreshes. It also needs a vsync-blocked present mode. SDL3's GPU API provides neither timestamp, so **D-19 (#19)
  must provide present and vsync timing on desktop**: for example `CAMetalDisplayLink` (macOS 14 and later) and
  `MTLDrawable` presented handlers on macOS (`CVDisplayLink` is deprecated from macOS 15), and Vulkan present timing
  or present-wait (`VK_EXT_present_timing`, `VK_KHR_present_wait` or `VK_KHR_present_wait2`) or Wayland's
  presentation-time protocol on Linux. D-35 (#35) supplies the Windows equivalent, such as DXGI frame statistics.
- When the timing source is unavailable or unreliable, playback falls back to audio master and reports why:
  no present timing on this output, an unstable refresh interval, variable-frame-rate content, or no cadence within
  the speed limit. The reason appears in the statistics snapshot and the log. The setting stays as set, so display
  resample resumes when the conditions allow it, for example after the window moves to another display.

### Frame selection

- At each refresh, the vsync thread hands the frame source the deadline interval for that refresh on the monotonic
  clock: when its image will appear and when the next one will. The frame source, which is shared code, maps the
  interval to media time through the clock and picks the frame. Today's `TakeDueFrame(MediaTime)` changes
  accordingly in #90.
- **Cadence estimator.** From the frame duration and the refresh interval, the estimator finds the repeating pattern
  of refreshes per frame, over up to five frames, whose drift from the clock stays acceptable the longest: [2:3] for
  24 fps at 60 Hz, [5] at 120 Hz, [6] at 144 Hz and [2:3:2:3:2] for 60 fps at 144 Hz. While a cadence holds, each
  frame is shown for exactly its count, and the slow drift between cadence and clock is corrected by single-refresh
  adjustments spaced as far apart as the rates allow.
- **Hysteresis.** While a cadence holds, the number of refreshes a frame gets comes from the pattern, not from
  comparing each frame's timestamp to the nearest vsync, so a frame whose due time lies near a refresh boundary
  cannot flip between neighbouring refreshes. A new cadence, after a refresh-rate change, a frame-rate change or
  sustained drift, takes over only after it has been estimated consistently for a minimum time. Flipping near
  boundaries turns [5:5:5:5] into [5:4:6:5], and it is the main source of micro-stutter at 120 Hz, 144 Hz and above.
- **No cadence.** For variable-frame-rate content or irregular timestamps, the frame covering most of the deadline
  interval is chosen, and if none covers it, the one closest to it, as Chromium does.
- **Faster content.** Content faster than the display, such as 120 fps on 60 Hz, shows the frames its cadence
  selects. Frames skipped by the cadence are counted separately from drops.
- The cadence makes motion as even as the two rates allow. It cannot remove the 3:2 judder of 24 fps on a 60 Hz
  display; only refresh-rate matching, or a display whose rate is a multiple of the frame rate, such as 120 Hz or
  144 Hz for 24 fps, removes it.

### Drops and resync

- An early frame is held until its refresh. A frame is dropped only when it is more than one frame interval late;
  `playback.sync.frame_drop_lateness_percent` (default 100, a percentage of the frame interval) sets the threshold.
  A frame less late than that is still shown.
- When video falls more than 500 ms behind the clock (`playback.sync.keyframe_skip_lag_ms`, Media3's threshold), the
  video decoder skips to the next keyframe and presentation resumes from there.
- Audio is never dropped or paused to let video catch up.
- Dropped frames, cadence-skipped frames and cadence breaks are counted separately in the frame counters and the
  statistics snapshot (ADR-0020).

### Route and latency changes

- On a route or device change, or when a sink reports a new latency, the reference player re-queries the latency.
  The clock slews to the new target over a few hundred milliseconds: video holds frames when audio became later and
  drops frames when audio became earlier. Audio plays on without a gap.
- If the offset is still outside the failure threshold when the slew window ends, the player resyncs hard: video
  holds or skips to the correct frame at once, through a keyframe skip if needed. Audio is not interrupted.
- `currentTime` never runs backwards: a later audio route slows or holds the clock instead of rewinding it.
- `playback.sync.audio_offset_ms` covers receivers and routes that report no latency or a wrong one. A positive
  value says audio is heard that much later than the sink reports, so video is delayed to match; a negative value
  does the opposite. A measured offset of −80 ms is corrected with a setting of 80.

### Playback rate and start

- From 0.25× to 4×, audio is time-stretched with its pitch preserved (#92). When `preservesPitch` is false, audio in
  that range is resampled instead, so its pitch follows the rate. Other finite positive rates are accepted with audio
  muted, as browsers do. The clock changes rate when audio processed at the new rate becomes audible, so a rate
  change does not shift the A/V offset.
- The clock starts when the first real audio sample is rendered: the first video frame is already on screen, paused,
  and the clock stays at the start position until that sample reaches the output. "Real" means content audio, not
  the silence a sink plays before data arrives. There is no black flash before the first frame and no A/V jump after
  it.
- The same rule applies after a seek, which resets the clock to the target (port contract), and on resume after a
  pause, which freezes the clock at the audible position.
- Video-only streams start the monotonic clock with the first presented frame.

### Display refresh rate

- Every video sink used by the reference player reports the display's current refresh rate, as a rational where the
  platform provides one, and reports each change: a mode switch or the window moving to another display. The
  cadence estimator and display resample use it. Direct ports report it where the platform exposes it, for
  statistics.
- SDL3 provides the nominal rate and change events on desktop. The WebCodecs port, which has no refresh-rate API,
  measures it from `requestAnimationFrame` timestamps.
- Requesting a matching refresh rate belongs to the ports. It is automatic where the operating system makes it
  seamless or governs it: `Surface.setFrameRate` with `CHANGE_FRAME_RATE_ONLY_IF_SEAMLESS` on Android and Google TV
  (D-21, #21), and tvOS display criteria, which the user's Match Content settings control (the Apple path, D-20,
  #20). On desktop it is opt-in (D-19, #19), because a mode switch can blank the monitor for a second or more.

### Port timing requirements

| Timing | Reported by | Requirement |
|--------|-------------|-------------|
| Media time | Every port, through `GetSnapshot` | Required (existing). Never decreases between seeks. |
| Audio output latency | Audio sinks and direct ports | Required (existing). The unknown sentinel is allowed, never a guessed zero. |
| Route, device or latency change | Audio sinks | Required (new). Triggers the re-query and slew above. |
| Refresh rate and its changes | Video sinks in the reference player; direct ports where available | Required for the reference player (new). |
| Vsync deadline interval | The vsync thread, to the frame source | Required for the reference player (new). |
| Presented-frame timestamps | Video sinks | Optional; refine drop accounting. Required for display resample. |
| Measured refresh interval and present timing | Desktop video sink (D-19) | Required for display resample. |

The new entries add pure virtual methods to the Penumbra interface, so #90 adds them under the port contract's
versioning rule, which increments `penumbra::kApiVersion`.

### Sync tolerance and measurement

This is the gate ADR-0021 deferred to D-02.

- **Offset.** For a media instant, the offset is the time its picture appears minus the time its sound is heard.
  Equivalently, at 1× it is the presentation timestamp of the audio being heard minus that of the video being shown
  at the same moment, the "audio minus video" form used in #2. A positive offset means audio leads (sound before
  vision), the sign convention of EBU R37 and ITU-R BT.1359. Offsets are wall-clock milliseconds at every playback
  rate.
- **Target.** In steady state, every measured |offset| is at most 20 ms.
- **Failure.** Any measured offset above +40 ms or below −60 ms, the EBU R37 bounds, fails the run, in any phase.
- **Content and capture.** The 10-minute flash and beep clip of #93, captured from real output as ADR-0021
  requires: loopback or frame capture where it represents the real output, otherwise a light sensor and microphone
  sampled on one clock. A camera is too coarse for this (a 60 fps camera resolves 16.7 ms). Each run measures and
  subtracts the capture path's own offset against a reference source with zero offset, and reports its uncertainty.
- **Phases.** Results are reported separately for startup, after a seek, after a rate change and in steady state.
  The first 5 seconds after the first frame, after each completed seek and after each rate change are transient;
  everything else is steady state. Transients are held to the failure threshold, and the time each takes to settle
  within 20 ms is reported. Each phase reports the median, 95th percentile and maximum offset, and the drift slope.
- **Routes.** Results record the audio route, the display and its mode, because receivers and display processing
  add latency the platform may not report. Bluetooth routes are reported but never fail a run, because their latency
  reporting varies too much to gate on.
- **Scope.** #90's M1 exit requires the target and the failure threshold on the desktop reference port with a
  built-in or wired output. ADR-0021's release gate requires the same measured evidence for each claimed output path.

### Settings

These keys follow [ADR-0017](0017-settings-schema-and-configuration.md#naming). All are `live` and `experimental`
to start, in a new `playback` area of the schema:

| Key | Type and default | Audience | Notes |
|-----|------------------|----------|-------|
| `playback.sync.mode` | enum `audio` or `display_resample`; `audio` | end user | Available on Linux, macOS and Windows. |
| `playback.sync.resample_max_change_percent` | double; 1.0 | end user | Largest speed change display resample may apply. |
| `playback.sync.audio_offset_ms` | signed integer; 0 | end user | Available wherever the port can apply it. Replaces #69's proposed `audio.output.latency_compensation_ms`, so there is one user offset. |
| `playback.sync.frame_drop_lateness_percent` | integer; 100 | integrator | Percentage of the frame interval. Replaces #90's proposed `playback.sync.late_frame_drop_ms`, because a fixed 16.7 ms is a whole frame at 60 fps but less than half of one at 24 fps. |
| `playback.sync.keyframe_skip_lag_ms` | duration; 500 | integrator | Video lag behind the clock that triggers a keyframe skip. |

The refresh-rate-matching key belongs to the ports' definitions (D-19, D-20 and D-21). It should be one shared key,
off by default, with port defaults turning it on where switching is seamless, through ADR-0017's port-default layer.

### Corrections to the issue

The proposal in #2 is corrected in five places to match accepted decisions:

1. The issue's "Umbra-driven pipeline" is ADR-0016's Penumbra reference player, so the sync code lives there, not
   in Umbra.
2. The proposed `MediaTimeProvider` maps onto the existing `Player::GetSnapshot`; no new interface is added.
3. The question of `int64_t` microseconds versus rational time bases is already closed by #51.
4. ADR-0021 accepted none of the issue's numbers. The tolerance above is set here, and it becomes the release gate.
5. The port contract rules out negative playback rates, so reverse playback is not considered.

The issue also proposed deferring display resample until audio master was measured, and following Starboard's
cadence pattern generator. The maintainer chose to offer display resample from M1, and the cadence follows
Chromium's estimator with hysteresis.

## Consequences

**Positive**
- Motion is as even as each frame-rate and refresh-rate pair allows, at 60 Hz, 120 Hz, 144 Hz and above, and clock
  jitter cannot cause micro-stutter.
- Users who enable display resample on desktop get no cadence breaks for near-matching rates, such as 23.976 fps on
  120 Hz.
- Audio stays untouched by default, and the default path is the one ffplay, Media3 and Chromium use.
- `currentTime` is steady and never runs backwards, which UI, captions and ad cue points rely on.
- Every port can meet the requirements, and ports that synchronise in the platform keep doing so.
- Sync is defined with a sign, a target, a failure threshold and a measurement method, so #93 can assert a number.
- Route changes, including to Bluetooth, are absorbed without an audible gap.

**Negative / costs**
- M1 carries more work: a resampling loop, display-resample control and a dependency on present timing from D-19.
- Display resample shifts pitch slightly (about 1.7 cents for 23.976 fps on 120 Hz, at most about 17 cents) and
  plays content up to 1% faster or slower.
- The smoothing loop and the cadence estimator need tuning and thorough tests.
- The Penumbra interface changes: refresh-rate reporting, deadline intervals and change notifications, and optional
  presented-frame feedback.
- The 20 ms target is tight for SDL3's estimated audio position until #93 proves it.
- Keyframe skips are visible jumps, and a slew after a route change shows a brief hold or skip in video.
- 3:2 judder on 60 Hz displays remains unless the display can switch to a matching rate.

**Follow-ups**
- #90 implements the clock sources, smoothing, resample ratio, display resample with its fallback reasons, the
  cadence estimator and hysteresis, frame selection, drops, keyframe skips, the route-change slew, the start rule
  and the Penumbra interface additions. Its deterministic tests run on ADR-0015's virtual time with fake audio sinks
  (bursty consumption, injected latency, ±100 ppm drift, route changes) and fake vsync clocks at 50, 59.94, 60,
  120, 144, 165 and 240 Hz against 23.976, 24, 25, 29.97, 30, 50, 59.94, 60 and 120 fps content. They assert exact
  cadences, the expected interval between cadence breaks, no flips under injected jitter, a monotonic `currentTime`
  and a smoothed clock within 1 ms of the simulated audible position in steady state.
- #90 also adds `settings/schema/playback.toml` with the five keys above, and the settings README gains the
  `playback` area's ownership.
- #92 implements the pitch-preserving time-stretch from 0.25× to 4×, resampling when `preservesPitch` is false,
  muting outside that range and applying the resample ratio. Its proposed `playback.rate.min` and
  `playback.rate.max` become the bounds of the audible range (0.25 and 4.0), not rejection bounds. Whether the public
  API rejects a rate outright follows D-03 (#3).
- #93 implements the offset definition, the capture calibration, the phases, the per-phase statistics and the route
  reporting, and rules on whether SDL3's estimated audio position meets the target.
- #69 reports output latency and route, device and latency changes from the desktop audio sink. #68 reports the
  refresh rate and its changes and hands the frame source the deadline interval; its planned present timestamps
  depend on D-19's timing source.
- #71 adds conformance cases for monotonic media time, latency reporting and refresh-rate reporting. #126 adds the
  sync mode and fallback reason, the estimated offset, the cadence and the separate drop, skip and break counts to
  the statistics.
- D-19 (#19) must provide present and vsync timing on desktop for display resample, decide how to obtain it next to
  SDL3's GPU swapchain, decide the opt-in desktop refresh-rate matching and its key, and decide whether variable
  refresh rate displays are driven at a multiple of the content rate.
- D-21 (#21) requests matching refresh rates on Android and Google TV with `Surface.setFrameRate`, seamless switches
  automatically and others only on opt-in. D-20 (#20) does the same on tvOS through display criteria and decides how
  the user offset applies on the Apple path.
- D-35 (#35) supplies Windows present timing if display resample ships there.
- D-13 (#13) maps this media clock to wall-clock time for live streams.

## Pros and cons of the options

### The master clock

- **A. Audio master, display resample later.** Good, because audio is untouched and the approach is well tried.
  Bad, because near-matching rates still repeat a refresh periodically, most often at high refresh rates, and the
  fix waits until after M1.
- **B. Audio master, display resample opt-in.** Good, because the default stays the low-risk path while users who
  want perfectly even motion can have it from M1. Bad, because it needs present timing that SDL3's GPU API lacks and
  a resampling loop in M1.
- **C. Display master on desktop.** Good, because desktop motion is the smoothest by default. Bad, because the
  reference port would behave unlike every other port, it carries the highest risk and it shifts pitch for everyone.
- **D. System-clock master.** Good, because several outputs are easy to synchronise. Bad, because audio is resampled
  all the time with no smoothness gain over A.

### What "in sync" means

- **A. 20 ms target, EBU R37 failure.** Good, because it is achievable with estimated sinks and stays well inside
  what people notice. Bad, because 20 ms is tight for SDL3's estimate until #93 proves it.
- **B. 10 ms target, ±20 ms failure.** Good, because it is showcase-grade. Bad, because it may fail on desktop sinks
  without presentation timestamps, forcing native sinks early.
- **C. BT.1359 detectability only.** Good, because it is the easiest to pass. Bad, because visible lip-sync errors
  can still pass.

### What ports report

- **A. Smoothed audio clock, optional presented frames.** Good, because it works on every port with jitter far below
  a 144 Hz refresh. Bad, because the filter needs tuning and tests.
- **B. Presented-frame feedback required.** Good, because drop counts are exact and display resample is always
  possible. Bad, because SDL3's GPU API and many browser paths cannot provide it, so ports would be blocked.
- **C. Raw sink position.** Good, because it is the simplest. Bad, because it jumps by a device buffer, which picks
  the wrong frames and judders visibly at high refresh rates.

### Route and latency changes

- **A. Re-query and slew.** Good, because there is no audible gap and sync is correct within a second. Bad, because
  video briefly holds or skips during the slew.
- **B. Pause and rebase.** Good, because sync is exact afterwards. Bad, because every route change causes an audible
  gap and a `waiting` blip.
- **C. Ignore until the next seek.** Good, because it is trivial. Bad, because Bluetooth leaves 100–300 ms of
  lip-sync error until the next seek.

### Drops, repeats and resync

- **A. Per-vsync selection with a cadence estimator.** Good, because it gives the most even pattern at every refresh
  rate, follows proven thresholds and recovers from CPU spikes without dropping audio. Bad, because keyframe skips
  are visible jumps and the estimator is real work.
- **B. Aggressive dropping.** Good, because sync is tighter. Bad, because jittery systems drop more visibly.
- **C. Never skip to keyframes.** Good, because there are no jumps. Bad, because a CPU-bound decoder can never catch
  up.

### Playback rate and start

- **A. Pitch-preserved 0.25–4×, start on audible audio.** Good, because it behaves like browsers and starts with no
  black flash and no A/V jump. Bad, because the muted range needs a hint in the UI.
- **B. 0.5–2× only.** Good, because time-stretch quality matters over a narrower range. Bad, because it diverges
  from HTML.
- **C. Video-led start.** Good, because the first frame plays sooner. Bad, because playback starts with an offset
  that then has to be corrected.

### Refresh-rate switching

- **A. D-02 contract, ports execute.** Good, because the layering is clean and the cadence is correct at any rate.
  Bad, because the work spans several definitions.
- **B. D-02 owns everything.** Good, because it is all in one place. Bad, because platform calls would sit in an
  engine-level decision.
- **C. Ports only.** Good, because D-02's scope is smallest. Bad, because the shared cadence estimator could not know
  the display rate.

## References

- [D-02 discussion](https://github.com/LuisAhumadaMartens/alumbra/issues/2)
- [EBU R 37: Relative timing of sound and vision components of a TV signal](https://tech.ebu.ch/publications/r037)
- [Recommendation ITU-R BT.1359: Relative timing of sound and vision for broadcasting](https://www.itu.int/rec/R-REC-BT.1359/en)
- [Chromium: media/filters/video_renderer_algorithm.h](https://chromium.googlesource.com/chromium/src/+/main/media/filters/video_renderer_algorithm.h) ·
  [video_cadence_estimator.h](https://chromium.googlesource.com/chromium/src/+/main/media/filters/video_cadence_estimator.h) ·
  [wall_clock_time_source.h](https://chromium.googlesource.com/chromium/src/+/main/media/base/wall_clock_time_source.h)
- [mpv manual: `--video-sync`](https://mpv.io/manual/stable/#options-video-sync) ·
  [options source, including `--video-sync-max-video-change` and `--video-sync-max-audio-change`](https://github.com/mpv-player/mpv/blob/master/DOCS/man/options.rst)
- [Media3: DefaultMediaClock](https://github.com/androidx/media/blob/release/libraries/exoplayer/src/main/java/androidx/media3/exoplayer/DefaultMediaClock.java) ·
  [AudioTrackPositionTracker](https://github.com/androidx/media/blob/release/libraries/exoplayer/src/main/java/androidx/media3/exoplayer/audio/AudioTrackPositionTracker.java) ·
  [MediaCodecVideoRenderer](https://github.com/androidx/media/blob/release/libraries/exoplayer/src/main/java/androidx/media3/exoplayer/video/MediaCodecVideoRenderer.java) ·
  [VideoFrameReleaseHelper](https://github.com/androidx/media/blob/release/libraries/exoplayer/src/main/java/androidx/media3/exoplayer/video/VideoFrameReleaseHelper.java)
- [FFmpeg: fftools/ffplay.c](https://github.com/FFmpeg/FFmpeg/blob/master/fftools/ffplay.c)
- [Cobalt: video_frame_cadence_pattern_generator.h](https://github.com/youtube/cobalt/blob/main/starboard/shared/starboard/player/filter/video_frame_cadence_pattern_generator.h)
- [SDL3: SDL_gpu.h](https://github.com/libsdl-org/SDL/blob/main/include/SDL3/SDL_gpu.h) ·
  [SDL_GetAudioStreamQueued](https://wiki.libsdl.org/SDL3/SDL_GetAudioStreamQueued) ·
  [SDL_GetCurrentDisplayMode](https://wiki.libsdl.org/SDL3/SDL_GetCurrentDisplayMode)
- [Apple: CAMetalDisplayLink](https://developer.apple.com/documentation/quartzcore/cametaldisplaylink) ·
  [MTLDrawable addPresentedHandler](https://developer.apple.com/documentation/metal/mtldrawable/addpresentedhandler(_:)) ·
  [CVDisplayLink](https://developer.apple.com/documentation/corevideo/cvdisplaylink-k0k) ·
  [AVDisplayManager](https://developer.apple.com/documentation/avkit/avdisplaymanager) ·
  [AVSampleBufferRenderSynchronizer](https://developer.apple.com/documentation/avfoundation/avsamplebufferrendersynchronizer)
- [Vulkan: VK_EXT_present_timing](https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_present_timing.html) ·
  [VK_KHR_present_wait](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_present_wait.html) ·
  [VK_KHR_present_wait2](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_present_wait2.html)
- [Wayland: presentation-time protocol](https://wayland.app/protocols/presentation-time)
- [Microsoft: IDXGISwapChain::GetFrameStatistics](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-getframestatistics)
- [Android: Surface.setFrameRate](https://developer.android.com/reference/android/view/Surface#setFrameRate(float,%20int,%20int)) ·
  [frame rate guidance](https://developer.android.com/media/optimize/performance/frame-rate)
- [HTML: `playbackRate`](https://html.spec.whatwg.org/multipage/media.html#dom-media-playbackrate) ·
  [`preservesPitch`](https://html.spec.whatwg.org/multipage/media.html#dom-media-preservespitch)
- [ADR-0015](0015-threading-and-task-runners.md), [ADR-0016](0016-penumbra-port-contract.md),
  [ADR-0017](0017-settings-schema-and-configuration.md),
  [ADR-0020](0020-observability-logging-tracing-qoe-metrics.md), [ADR-0021](0021-testing-strategy.md),
  [port contract](../architecture/port-contract.md), [media types](../architecture/media-types.md)
