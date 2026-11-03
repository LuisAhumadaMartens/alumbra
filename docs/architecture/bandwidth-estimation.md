# Bandwidth estimation

[ADR-0033](../adr/0033-abr-algorithm-and-bandwidth-estimation.md) fixes the network estimate as dual fast and slow
EWMAs with a separate TTFB estimate. The internal interface is in `umbra/abr/`; it has no dependency on a concrete
HTTP implementation.

`BandwidthEstimatorConfig` uses the same field names and units as the `abr.*` settings. The defaults are 1 Mbps for
the initial estimate, 2 seconds for the fast half-life, 5 seconds for the slow half-life and 16 KiB for the minimum
sample size. These numeric defaults were not selected by ADR-0033, so #122 should tune them against the simulation
traces.

The request callback path may call `AddChunkSample` with the bytes and active transfer duration represented by a
chunk. The transport measures chunk arrival relative to the preceding chunk, so idle time between arrivals is not
included. For coarse timing transports such as web `fetch`, the completion callback calls `AddSample` with the
whole body size and the interval from response start to completion. TTFB is supplied separately in both cases.
Samples below the configured size, samples with invalid timing and requests identified as cache hits are ignored.

Before a qualifying sample, `GetEstimate` returns the configured initial estimate and zero samples. After each
accepted sample it reports the lower of the fast and slow estimates, TTFB and the accepted sample count. Each
accepted result is also sent to `umbra::observability` as `BandwidthEstimateMetric` for QoE statistics.

Tests use synthetic durations and do not wait on a clock. `ctest --preset debug -R umbra_abr_unittests` runs the
estimator suite.
