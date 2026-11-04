# Network error recovery and redundant streams

The recovery ladder of [ADR-0018](../adr/0018-error-model-and-recovery.md) lives in `umbra/fetch/`, namespace
`umbra::fetch`, beside the [segment fetcher](segment-fetching.md). It keeps playback going through network failures
when the manifest offers an alternative, and fails with a catalogue error when it does not. Like the fetcher, it
includes no platform header, runs on the control sequence ([ADR-0015](../adr/0015-threading-and-task-runners.md)) and
tests against the fake clock and the scripted transport. Only the network steps of the ladder (steps 1 to 3 and the
fatal step) are here. Decoder fallback (step 4) belongs to D-06 (#6).

## Shape

| Header | Contents |
|--------|----------|
| `fetch_failure.h` | `FetchFailure`, the status payload that carries the URL, HTTP status, request class and a valid `Retry-After`; `ParseRetryAfter` |
| `backoff.h` | `BackoffPolicy`, `JitterSource`, `SeededJitter`, `BackoffDelay` |
| `error_classifier.h` | `ClassifyFailure`, `Classification`, `LivePosition` |
| `pathway_table.h` | `PathwayTable`: exclusion deadlines per pathway or per pathway and rendition |
| `content_steering.h` | `SteeringManifest`, `SteeringParser`, `SteeringListener`, `SteeringClient` |
| `recovery_ladder.h` | `RecoveryLadder` and the small interfaces it depends on |
| `recovery_errors.h` | D-09 error construction and `ManifestLoadRecovery` |

## The ladder

`RecoveryLadder` is a `SegmentSink` decorator. The player gives it to the fetcher as the sink and selects tracks
through `RecoveryLadder::SelectTrack` instead of calling the fetcher directly, so the ladder always knows the active
track and its alternatives. `SetTarget` connects it to the fetcher (`FetcherRecoveryTarget`), which breaks the
construction cycle. Segments, init segments and indexes pass through unchanged, and a delivered one (not a cached
init segment) resets the attempt budget of that stream.

When the fetcher reports `OnStreamError`, the ladder posts a task to the control sequence, so the fetcher is never
re-entered from its own error path. It then classifies the failure and escalates:

1. **Retry.** The same URL is retried with exponential backoff and jitter, `network.retry.max_attempts` times. The
   delay ceiling of attempt *n* (from zero) is `min(max_backoff, initial_backoff * 2^n)`, and the delay is the equal
   jitter `ceiling / 2 + unit * ceiling / 2`, with `unit` in [0, 1) from a `JitterSource`. Production code seeds a
   `SeededJitter` (a Mersenne Twister whose output is mapped to a unit interval without library distributions, so
   the sequence is identical on every standard library). Tests inject a fixed source.
2. **Fail over.** The ladder moves to the best other source of the same rendition. A source is a pathway, a track and
   a base URL, so one list covers HLS duplicate variants and pathway clones (different tracks) and DASH `BaseURL`
   elements (one track, several base URLs). The failed source is excluded, then the stream is reselected with
   `SwitchMode::kCancelInFlight` and resumes at the last delivered time.
3. **Exclude the rendition.** When no source is left, the rendition is excluded for `abr.rendition_exclusion_ms`
   (60 s), `RenditionControl::ExcludeRendition` tells the ABR filter, and `SelectAlternative` returns the rendition
   that selection picks instead. The ladder restores the rendition when the time is up. This also covers a segment
   that one representation lacks.
4. **Fatal.** When nothing is left, the stream reports a D-09 error. The ABR step is skipped when the cause is
   `401` or `403`, because another rendition on the same credentials would fail the same way.

The `streaming.failover.enabled` setting gates steps 2 and 3. When it is false, only step 1 runs.

## Classification

`ClassifyFailure` is a pure function of the status code, the `FetchFailure` payload and a `LivePosition` supplied by
an optional `LivePositionProbe`.

| Failure | Action |
|---------|--------|
| `Unavailable`, `DeadlineExceeded`, `DataLoss` (timeouts, resets, `408`, `429`, `5xx`, a wrong body size) | Retry |
| The same with a valid `Retry-After` up to `network.retry.max_backoff_ms` | Retry after exactly that delay |
| The same with a longer `Retry-After` | Fail over at once and hold the pathway until the deadline |
| `429` without a valid `Retry-After` | Ordinary jittered backoff |
| `NotFound` for a live segment behind the window | Resynchronise to the window (`RecoveryTarget::Resync`) |
| `NotFound` near the live edge | Retry |
| Other `NotFound` | Fail over, excluding only that rendition on that pathway |
| `PermissionDenied` | Never retried; fail over, then fatal |
| `InvalidArgument`, `OutOfRange` (other `4xx`, a range answered in full) | Not retried; fail over |
| Anything else | Fatal |

A `Retry-After` is valid when it is a number of seconds, at most one day. An HTTP date is treated as absent,
because Umbra has no wall clock in this layer, and so ordinary backoff applies. The fetcher attaches the payload,
which is why the status code alone is enough for everything else.

A resync is bounded by the same attempt budget as retries, so a window that keeps moving cannot loop.

## Exclusion

`PathwayTable` keeps one entry per key. A pathway-wide failure (retries exhausted, `401`, `403`, a long
`Retry-After`) excludes the pathway. A failure that concerns one representation (`404`, other `4xx`, a stale playlist)
excludes the pair of pathway and rendition, so the other renditions on that CDN keep working.

- **Without steering,** an exclusion lasts `network.pathway_exclusion_ms` (300 s).
- **With steering,** it lasts until the next steering refresh, capped at the same duration, so a steering client that
  stops refreshing cannot exclude a pathway for ever. Applying a refresh clears these exclusions.
- **A `Retry-After` hold** is a separate deadline. A refresh never clears it, so a pathway that asked for more than
  the maximum backoff stays out until both the exclusion and the hold are over, while the other pathways are tried at
  once.

A selection with every source excluded still plays the first source, because refusing to play is worse than trying.

## Redundancy and steering

The parsers (#426, #427) and the ABR filter (#428, #433) are in review, so the ladder depends on three small
interfaces and does not include their headers:

- `RedundancyProvider::SourcesFor(track)` returns the ordered sources of a rendition, the primary first. It also
  receives `PathwayClone` entries from each steering refresh so that the HLS parser can build the cloned pathways.
- `RenditionControl` excludes and restores renditions and returns the alternative that selection picks.
- `RecoveryObserver` receives one `RecoveryEvent` per step (retry, resync, failover, exclusion, restoration, steering
  applied, fatal), which feeds the logs and the QoE statistics (#126).

`SteeringClient` polls the steering manifest through the HTTP transport as a manifest-class request. The first poll is
immediate, the next follows after the manifest `TTL` (at least `min_ttl`, one second), and a changed `RELOAD-URI`
replaces the URL. A failed poll keeps the previous manifest and retries after `failure_retry` (300 s). The JSON
parsing stays behind `SteeringParser`, because the tree has no JSON library yet. `RecoveryLadder` implements
`SteeringListener`: it orders pathways by `PATHWAY-PRIORITY` (a stable sort, with unlisted pathways last so that
failover can still use them) and, when the best source changes, selects it with `SwitchMode::kAfterInFlight`, so the
requests already queued finish on the old pathway and later ones move. `streaming.content_steering.enabled` turns the
ordering off. The HLS refresh policy (#451) reports a stale playlist through `RecoveryLadder::OnPlaylistFailover`,
which fails over only when another source exists and otherwise keeps the stream playing.

## Errors

A fatal result is `ALUMBRA-1003` (`MEDIA_ERR_NETWORK`), or `ALUMBRA-1002` when the last cause was `401` or `403`. The
context holds the redacted `url`, the `http_status`, the number of `attempts` and the canonical `cause`.
`ManifestLoadRecovery` applies the same classification to the manifest load: a `4xx` is fatal at once, and timeouts and
`5xx` retry with the same backoff and then fail with the same error, with the manifest URL and status in its detail.

## Settings

| Key | Use |
|-----|-----|
| `network.retry.max_attempts`, `network.retry.initial_backoff_ms`, `network.retry.max_backoff_ms` | `RecoveryConfig::backoff` |
| `abr.rendition_exclusion_ms` | `RecoveryConfig::rendition_exclusion` |
| `network.pathway_exclusion_ms` | `RecoveryConfig::pathway_exclusion` |
| `streaming.failover.enabled` | `RecoveryConfig::failover_enabled` |
| `streaming.content_steering.enabled` | `RecoveryConfig::content_steering_enabled` |

Like `FetchConfig`, `RecoveryConfig` is a plain struct that the player fills when it wires the engine.

## Tests

`test/umbra/fetch/` runs the ladder on `TestTaskEnvironment` against the real `SegmentFetcher` and a routing transport
made of one scripted `FakeHttpTransport` per host. Cases cover retry counts and delays, a host that starts failing
mid-playback and the failover to a redundant variant and to another `BaseURL`, `429` with and without `Retry-After`,
a long `Retry-After` that outlives a steering refresh, a segment that one rendition lacks, exclusion and restoration
times, steering reorder and `TTL`, live resync, the disabled settings and cancellation. Fault-injection tests against
the in-tree HTTP server join once the libcurl transport (#75) is in the tree.

## References

- [ADR-0018: Error model and recovery](../adr/0018-error-model-and-recovery.md)
- [HLS Content Steering](https://developer.apple.com/streaming/HLSContentSteeringSpecification.pdf)
- [ETSI TS 103 998: Content Steering for DASH](https://www.etsi.org/deliver/etsi_ts/103900_103999/103998/01.01.01_60/ts_103998v010101p.pdf)
- [RFC 9110: 429 and Retry-After](https://www.rfc-editor.org/rfc/rfc9110#field.retry-after)
