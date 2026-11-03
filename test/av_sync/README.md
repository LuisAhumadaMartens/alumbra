# A/V synchronisation measurement

`tools/av_sync/av_sync.py` analyses a capture produced by a real playback port or
by a deterministic test. The capture timestamps must use one monotonic clock.
`video_present_ms` is the instant a flash became visible, and
`audio_audible_ms` is the instant its matching beep became audible. Both are
wall-clock milliseconds from the same origin. `media_time_ms` is the marker's
media position and can reset after a seek. Events must be ordered by increasing
capture time. Drift uses the average of each event's video and audio capture
timestamps, so it measures wall-clock drift at any playback rate.

The capture JSON schema is version `1`. A real capture provides frame luminance
and audio level windows so the analyser can find rising flash and beep edges.
Each video frame carries its presentation time, normalised average luminance and
media position. Audio windows carry their captured sample position and
normalised level. The sample clock anchor maps audio sample positions to the
same wall-clock domain as the frame timestamps:

```json
{
  "schema": 1,
  "metadata": {
    "route": "wired output",
    "display": "display name and mode"
  },
  "calibration": {
    "offset_ms": 0.0,
    "uncertainty_ms": 0.5
  },
  "frames": [
    {
      "media_time_ms": 1000.0,
      "present_ms": 1024.0,
      "luma": 0.96,
      "phase": "steady_state"
    }
  ],
  "audio": {
    "sample_rate_hz": 48000,
    "first_sample_position": 0,
    "first_sample_capture_ms": 0.0,
    "windows": [
      {"sample_position": 48000, "level": 0.42}
    ]
  }
}
```

Frame luminance and audio levels are in the range zero to one. Frames use the
`--flash-threshold` setting (default `0.8`); audio window levels use
`--beep-threshold` (default `0.1`). The audio capture reports a 1 kHz
band-limited RMS level per window rather than serialising every PCM sample. The
analyser finds rising threshold edges, pairs flashes and beeps by order and
rejects a capture when their counts differ. A detected frame may include the
phase field; it defaults to steady state.

A producer that has already detected onsets can instead supply paired events,
as below. This form is used for injected-offset tests and lets a later port
write either form while keeping the same analyser and report:

```json
{
  "schema": 1,
  "metadata": {
    "route": "wired output",
    "display": "display name and mode"
  },
  "calibration": {
    "offset_ms": 0.0,
    "uncertainty_ms": 0.5
  },
  "events": [
    {
      "media_time_ms": 1000.0,
      "video_present_ms": 1024.0,
      "audio_audible_ms": 1000.0,
      "phase": "steady_state"
    }
  ]
}
```

`metadata` is optional and retained in the report. The calibration offset is
measured using a zero-offset reference through the same capture path and is
subtracted from every event. Its uncertainty is reported with the measurements.
Phases are `startup`, `after_seek`, `after_rate_change` and `steady_state`; an
omitted phase means steady state. Captures should omit the first five seconds
after startup, a completed seek or a rate change from the steady-state phase.

For an event, the signed offset is `video_present_ms - audio_audible_ms -
calibration.offset_ms`. A positive value means audio arrived first. The analyser
reports mean and median signed offset, the signed minimum and maximum, p95 and
maximum absolute offset, and a least-squares drift slope in milliseconds per
minute and parts per million for each phase and for the whole capture.
Transient phases also report the time from their first marker until all
remaining markers are within 20 ms. A phase is marked as drifting when its
absolute slope exceeds 50 ppm; pass `--drift-threshold-ppm` to set another
threshold. The default is a tooling threshold for detecting rate skew, not a
separate playback tolerance.

ADR-0024 defines the steady-state target as ±20 ms and a hard failure above
+40 ms or below −60 ms in any phase. `--enforce` returns failure when a
steady-state target or hard bound is exceeded. Bluetooth measurements should
be recorded but are not used as a gating route. A route value in
`metadata.route` containing `Bluetooth` marks the run exempt from failure while
keeping measured threshold results in the report. The report includes the target
and hard-bound results, the calibration uncertainty, route and display metadata,
and drift metrics. The SVG plot draws the target and hard-bound lines.

Generate a ten-minute clip by default. The supported combinations are MP4 with
H.264/AAC, MPEG-TS with H.264/AAC, and WebM with VP9/Vorbis. The flash and beep
repeat at the same media instants, with video frame counters supplied by
FFmpeg's `testsrc` source. The frame rates are 23.976, 25, 29.97, 50 and 59.94
fps; audio rates are 44.1 and 48 kHz.

```sh
python3 tools/av_sync/generate_clip.py /tmp/av-sync.mp4 \
  --container mp4 --fps 23.976 --sample-rate 48000
python3 tools/av_sync/av_sync.py analyse test/av_sync/capture.json \
  --json /tmp/av-sync-report.json --plot /tmp/av-sync-report.svg --enforce
```

The regular CI workflow runs the analyser self-tests and generates a short,
probed clip for each container on Linux when the classifier detects relevant
changes. The separate weekly/manual full workflow generates and probes the
ten-minute variants for MP4, MPEG-TS and WebM at every listed frame and audio
rate on Linux and macOS. Both upload JSON and SVG reports from synthetic capture
data; they validate the tooling and do not claim real-output synchronisation. The
presenter readback, audio loopback and media clock needed for a real playback
capture are follow-ups to issues #68, #69 and #90. A real-output CI measurement
remains a follow-up once those components can provide calibrated captures.
