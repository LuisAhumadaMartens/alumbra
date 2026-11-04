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

Reports are deterministic JSON or Markdown. They contain rebuffer count,
duration and ratio, startup delay, switch count and rate, time-weighted and
sustainable bitrates, bitrate efficiency, and the score `sum(log2(bitrate /
300000) * segment_seconds) - 4.3 * rebuffer_seconds - switch_count`. The score
is a linear utility and penalty model for comparing replays, not a user-facing
QoE metric. The deterministic suite checks repeated reports byte for byte and
runs every committed synthetic trace in CTest.
`--compare` reports the default policy configuration alongside a conservative
throughput configuration using lower startup and steady-state safety factors;
the production defaults are unchanged.

`test/umbra/abr/baseline.csv` records the current default-policy result for each
synthetic trace and provisional limits derived from it. The rebuffer ratio is
rebuffer milliseconds divided by simulated playback milliseconds plus rebuffer
milliseconds. Bitrate efficiency is the time-weighted selected bitrate divided
by the trace's duration-weighted throughput. The trace's first and final listed
timestamps define the observation points; each point's throughput is held until
the next point, and the final value is held for the preceding interval. Switch
rate is rendition switches per simulated playback minute.

The upper limits for rebuffer ratio and switch rate are 110% of baseline, and
the lower limit for bitrate efficiency is 90% of baseline. These 10% margins
are provisional starting points from D-11, not real-device calibration. A
baseline value of zero produces a zero upper limit. `abr_simulator_synthetic_suite`
runs all committed CSV traces in the existing debug CTest job and fails with a
per-trace Markdown report when any limit is crossed. The test also checks that
repeated runs produce byte-identical reports.

After an intentional ABR change, build the simulator and regenerate the
baseline with:

```sh
cmake --build --preset debug --target abr_simulator
build/debug/test/umbra/abr/abr_simulator --suite test/umbra/abr/traces \
  --baseline test/umbra/abr/baseline.csv --regenerate-baseline
```

Review the raw metrics and derived limits in the changed baseline before
committing it. Update the provisional margins only after D-11's real-device
tuning provides reviewed thresholds.

Real-manifest segment-size modelling remains a follow-up. The existing
conformance asset fetcher does not provide rights-cleared stream fixtures for
this purpose.
