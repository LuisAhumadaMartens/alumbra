# 0020 — Build a platform-neutral observability facade for playback

- **Status:** Accepted; URL-redaction policy refined from [ADR-0018](0018-error-model-and-recovery.md#redaction) by [Privacy and redaction](#privacy-and-redaction)
- **Date:** 2026-10-31
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #30

## Context

Playback failures need a correlated account of network, buffering, adaptation, demux, decode, render and
audio work. The observability model also defines public QoE statistics, the debug overlay, and optional
client data sent with media requests. These APIs cross Umbra, Penumbra and Antumbra, so their categories,
units, privacy rules and disabled cost must be decided before instrumentation spreads through the engine.

The existing logging candidates do not provide the shape this needs: Abseil logging is not a structured,
category-oriented facade, while spdlog adds another general-purpose logging dependency. Tracing also needs
platform-specific collection and a web implementation that does not ship Perfetto.

## Decision drivers

- Structured fields and category gates shared by logs and traces.
- No general-purpose logging backend dependency in the portable engine.
- One bounded, queryable observability model, with the overlay and public statistics using the same data.
- Standards-compatible QoE and CMCD semantics without claiming that Alumbra-specific metrics are standardized.
- No secrets in default diagnostics and no unsolicited telemetry.
- No measurable disabled overhead, and a bounded enabled-tracing cost.

## Considered options

1. **A — Use Abseil logging directly** and add tracing separately.
2. **B — Use spdlog for logging and Perfetto for tracing.**
3. **C — Own a small facade** with platform-installed sinks, native tracing backends and typed metrics.

## Decision

Chosen option: **C.** Umbra owns the stable observability API; Penumbra installs output sinks and platform
integrations. This keeps engine code independent of logging backends while making categories, structured
fields, traces and metrics consistent across platforms. Perfetto is the only additional tracing dependency,
and is excluded from web builds.

### Logging API and categories

- `ALUMBRA_LOG(category, level)` accepts structured fields. Formatting and field construction are lazy and
  happen only when both the compile-time floor and runtime category/level gates admit the message.
- Penumbra installs the platform output: stderr on desktop, `os_log` on Apple platforms, logcat on Android,
  and `console` on web.
- Hot-path logging has rate-limited forms. The compile-time floor and runtime filters are settings-backed
  and follow [ADR-0017](0017-settings-schema-and-configuration.md).
- Logs and traces use the same categories: `net`, `manifest`, `abr`, `buffer`, `demux`, `decode`, `render`,
  `audio`, `text`, `ads`, `drm`, `settings`, `player` and `port`.
- Structured field names use lower snake case and unit suffixes consistent with settings, such as
  `throughput_bps` and `buffer_ms`.
- Alumbra does not use spdlog or Abseil logging as a backend.

### Tracing

- `ALUMBRA_TRACE(category, name)` provides scopes and counters, with category checks before event data is
  evaluated.
- A bounded in-process ring buffer supports dump-on-demand. Native Perfetto track events and system tracing
  are available where the target platform supports the relevant backend; this does not imply a Perfetto
  system service on macOS.
- Apple traces integrate with Instruments through `os_signpost`. Web traces use
  `performance.mark()` and `performance.measure()`; the Perfetto SDK is not included in web builds.
- Native Perfetto integration is build-configurable. The SDK's in-process backend records within the
  application; its system backend requires a running Perfetto daemon.

### QoE metrics and statistics

- A typed metrics-event stream feeds one statistics snapshot/model. The public stats API and debug overlay
  consume that same source of truth; the overlay does not calculate a second set of metrics.
- The curated public subset is time to first frame/startup, startup failures, rebuffer count and ratio,
  average and time-weighted bitrate, rendition switches, dropped frames and live latency. These public
  fields carry explicit stability labels. Additional diagnostic values live only in an explicitly unstable
  debug extension.
- Where a metric maps exactly to CTA-2066, Alumbra uses its defined name and calculation. The public
  version defines `Average Initial Startup Time`, `Playback Failure Percentage`, `Average Playback
  Stalled Count`, `Average Stalled Time Percentage` and `Average Playback Bitrate`; the latter is
  calculated from bits played divided by media time. Its session-level fields and events include
  `initialStartupTime`, `playbackFailed`, `playbackStallCount`, `playbackStallDuration`, `bitsPlayed`,
  `watchedTime`, `playbackStart`, `playbackStall` and `renditionUpdate`.
- CTA-2066's `Playback Failure Percentage` covers playback failure anywhere in a session; it is not a
  startup-failure metric. `renditionUpdate` is an event, not a standardized rendition-switch aggregate.
  Dropped frames and live latency are not metrics defined by the cited public CTA-2066 version. These
  values, and any other non-matching metric, use documented Alumbra-specific names, semantics and units;
  they are not presented as CTA-defined.
- Alumbra's unweighted average bitrate is distinct from CTA-2066's time-weighted `Average Playback
  Bitrate`. The implementation issue must specify the sample population for the unweighted statistic
  before implementation.
- Analytics callbacks are integrator opt-in. Alumbra does not make unsolicited analytics or phone-home
  requests.

### CMCD and CMSD

- CMCD v1 supports header and query transmission modes and is off by default. When enabled, Alumbra sends
  only applicable v1 keys for values the player knows, using the standard's required types, units and
  applicability rules. It does not send unknown or inapplicable values.
- `sid` is random and scoped to one playback. `cid` is sent only when the integrator supplies a content ID.
  CMCD v2 event reporting is deferred.
- CMSD is limited to server-provided estimated-throughput and maximum-suggested-bitrate hints for ABR.
  Hints cannot override safety checks or user-configured bounds. CDN steering remains the responsibility
  of [ADR-0018](0018-error-model-and-recovery.md).
- Explicitly enabled CMCD on normal playback requests is not unsolicited telemetry; other analytics
  transmission remains integrator opt-in.

### Privacy and redaction

- By default, logs, traces, error contexts and telemetry diagnostics contain only the origin and a short
  per-playback keyed correlation digest computed from the full URL. The path, query, fragment and user
  information are not retained in diagnostic output. The key is ephemeral and is not a stable
  cross-playback identifier.
- The digest is correlation metadata, not encryption or proof that a URL contains no secret. Truncation can
  collide, and a leaked key or other side channel can weaken its protection; it must not be used for
  authorization. Opaque URL tokens cannot be recognized safely for every token format.
- Full-URL diagnostics require an explicit debug opt-in. URL user information and credential values,
  authentication/cookie/configured-token headers, and DRM/license request or response bodies remain
  prohibited in every mode. Arbitrary token-like path or query values cannot be classified safely for
  every token format, so full-URL diagnostics are not safe for sources with opaque URL credentials and
  must not be enabled for them. The opt-in does not waive the credential bans. Redaction tests include
  tokens embedded in URL paths as well as query values and headers.
- This refines only the URL-redaction portion of [ADR-0018](0018-error-model-and-recovery.md#redaction);
  its other error and recovery decisions are unchanged.

### Cost budget

- With observability compiled out, macros do not evaluate their arguments and emit no category strings.
  With logging and tracing disabled at runtime, measured overhead must be within benchmark noise compared
  with the same workload without observability call sites.
- Enabled tracing must add no more than 3% CPU on the reference desktop workload. The comparison uses the
  same build, stream, device and playback conditions, with trace capture disabled as the baseline; the
  benchmark report records the workload and measurement conditions.
- [D-31](https://github.com/LuisAhumadaMartens/alumbra/issues/31) and #263 own Google Benchmark CI
  comparisons and baseline reporting. This decision does not assert that a performance-budget CI gate
  already exists.

## Consequences

**Positive**

- Logging, tracing and QoE data share a stable, platform-neutral model and category vocabulary.
- Apps and the debug overlay see the same metrics, while CTA-defined and Alumbra-specific semantics remain
  distinguishable.
- URL correlation is possible without retaining URL path, query or fragment by default.
- Alumbra does not collect or transmit analytics without an explicit integrator choice.

**Negative / costs**

- Alumbra owns the facade, bounded ring buffer, platform sink integrations and redaction tests.
- Platform tracing capabilities differ; the in-process ring buffer remains available where a system
  tracing backend is not.
- The short URL digest is not a security guarantee, and full-URL debug output remains risky for opaque
  token formats.
- Public metrics require stable semantics and stability labels; non-standard values need Alumbra-specific
  definitions.

**Follow-ups**

- #50 implements the facade, platform sinks, lazy gates, rate limiting, bounded ring buffer and URL-path
  redaction regression coverage.
- #126 implements the typed metrics stream, stable statistics snapshot, debug extension and shared
  overlay data model.
- #264 implements Perfetto integration where supported, while keeping web on User Timing.
- #263 measures overhead and reports Google Benchmark comparisons with explicit workload and baseline
  details under D-31.
- #53 adds the observability settings to the generated settings schema.

## References

- [D-30 discussion](https://github.com/LuisAhumadaMartens/alumbra/issues/30)
- [D-31 testing strategy](https://github.com/LuisAhumadaMartens/alumbra/issues/31)
- [ADR-0017: Settings schema](0017-settings-schema-and-configuration.md)
- [ADR-0018: Error model and recovery](0018-error-model-and-recovery.md)
- [CTA-2066: Streaming QoE events, properties and metrics](https://shop.cta.tech/products/cta-2066)
- [CTA-WAVE: CTA-2066 public version](https://github.com/cta-wave/R4WG20-QoE-Metrics/blob/master/CTA-2066_Public_Version.pdf)
- [CTA-5004-B: Common Media Client Data](https://cta-wave.github.io/Resources/common-media-client-data--cta-5004-b.html)
- [CTA-WAVE: CMCD specification repository](https://github.com/cta-wave/common-media-client-data)
- [Perfetto: Tracing SDK](https://perfetto.dev/docs/instrumentation/tracing-sdk)
- [Perfetto: License](https://github.com/google/perfetto/blob/main/LICENSE)
- [Apple: Recording performance data with signposts](https://developer.apple.com/documentation/os/recording-performance-data)
- [W3C: User Timing](https://www.w3.org/TR/user-timing/)
