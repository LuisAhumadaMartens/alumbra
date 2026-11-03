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

## Rendition policy

The internal `AbrPolicy` consumes a snapshot of bandwidth, buffer, current selection and candidate renditions. It
passes candidate descriptors, capability results and filter inputs to `FilterRenditions`, then gives only the
eligible candidates to its selection rules. The filter applies decoder, viewport, user, data-saver, CMSD and
decode-late caps and returns their exclusion reasons. The resulting `AbrDecision` includes those reasons alongside
the selected rendition and ABR reason. The selection rules keep frame rate fixed unless a caller explicitly
forces a change. At low buffer it uses a conservative throughput
budget; once buffer is healthy it uses a buffer utility score, with HLS `SCORE` resolving equal choices. A switch
cooldown and a minimum buffer for up-switches limit oscillation. The `abr.switch_buffer_ms` setting gates only
switches to a higher rendition; it does not choose between throughput and buffer-based selection and does not gate
down-switches. That split remains controlled by the policy's low-buffer threshold. The default up-switch threshold
is 8000 ms. If the current segment is projected to outlast the buffer and a lower rendition is projected to finish
sooner, the decision requests abandonment.

`abr.max_height` and `abr.max_bitrate_bps` have `next_load` apply semantics. The caller takes a settings snapshot
when a load starts and supplies its values to each decision for that load. `abr.cap_to_viewport` is live; the
caller supplies its current value and viewport height for each decision, so resizing can change the eligible set
without refreshing the other caps.

`AbrPolicyConfig` is constructed from the `abr.*` settings and policy inputs. The decision returns a stable reason
string for switch diagnostics and the filter exclusions for diagnostics or source errors. The policy has no
platform or manifest dependency; the caller supplies candidates and capability results from the active presentation
combination.
