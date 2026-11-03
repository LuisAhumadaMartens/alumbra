# ABR simulation

The test-only `abr_simulator` executable replays a finite network trace through
the production `BandwidthEstimator` and `AbrPolicy`. It uses a fake tick clock,
with no sockets or wall-clock waits. Each completed segment feeds its byte count
and transfer duration back into the estimator. The policy sees the current
buffer, rendition ladder, estimated bandwidth and logical time before the next
request.

Run it from the repository root:

```sh
build/debug/test/umbra/abr/abr_simulator \
  --trace test/umbra/abr/traces/step_drop.csv --format json --compare
```

The runner accepts CSV traces with the header `time_ms,throughput_bps`, where
each row changes the available throughput at that time. It also accepts
Mahimahi packet delivery traces with a `.mahimahi` or `.mm` suffix. Each line is
a millisecond timestamp for delivery of one 1500-byte packet; the simulator
groups those events into one-second throughput intervals. Inputs are finite,
non-negative and bounded to one hour.

The committed trace suite is generated for this project and covers a step drop,
oscillation, a rising ramp and an outage. These traces have no third-party
provenance or external licence. The initial rendition ladder and four-second
segment duration are synthetic because no redistribution-cleared real manifest
fixtures are present in the repository. The runner derives each segment's byte
size from the selected rendition bitrate and duration. Real manifest segment
sizes and public datasets can be added after the asset fetcher and rights
records are available.

Reports are deterministic JSON or Markdown. They contain rebuffer count and
duration, startup delay, switch count, time-weighted bitrate and the score
`sum(log2(bitrate / 300000) * segment_seconds) - 4.3 * rebuffer_seconds -
switch_count`. The score is a linear utility and penalty model for comparing
replays, not a user-facing QoE metric. The deterministic suite checks repeated
reports byte for byte and runs every committed synthetic trace in CTest.
`--compare` reports the default policy configuration alongside a conservative
throughput configuration using lower startup and steady-state safety factors;
the production defaults are unchanged.

ADR-0033 assigns quality regression thresholds to D-11. Neither that decision
nor the issue record defines numeric thresholds, so CI currently blocks on trace
replay and report determinism only. Threshold comparisons should be added when
maintainers record calibrated values.
