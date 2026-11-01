# 0018 — Carry errors as `absl::Status` with a public catalogue and one recovery ladder

- **Status:** Accepted
- **Date:** 2026-10-31
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #9

## Context

Alumbra follows Google C++ style and uses no exceptions, so errors travel as values across task boundaries
([ADR-0015](0015-threading-and-task-runners.md)), the port contract ([ADR-0016](0016-penumbra-port-contract.md))
and the language bindings (D-18, #18). They finally surface as an HTML-shaped `MediaError` (D-03, #3), whose four
codes (1 ABORTED, 2 NETWORK, 3 DECODE, 4 SRC_NOT_SUPPORTED) are far too coarse to operate a service: a 403 on a
segment, a Widevine provisioning failure and a malformed `moof` deserve distinct, stable codes.

Most media errors should never reach the app. Transient HTTP failures are retried, a dead CDN is failed over, a
failing decoder is replaced and a small buffer gap is skipped. Who does that, and in which order, is part of the
model.

## Decision drivers

- Google-style errors as values that cross threads, the port boundary and bindings cheaply.
- Stable public codes precise enough for operations, telemetry and support.
- Recovery that lives in one place and escalates predictably.
- Secrets and tokens never reach logs, telemetry or error messages by default.

## Considered options

1. **A — `absl::Status` with a structured payload** (`ErrorDetail` attached with `SetPayload`).
2. **B — Typed status per subsystem**, as Chromium's `media::TypedStatus<T>`.
3. **C — A single flat error-code catalogue**, as Shaka Player and Media3's `PlaybackException`.

## Decision

Chosen option: **A internally, with C's catalogue as the public face.** `absl::Status` is Google's idiom and is
cheap to pass across sequences and bindings; B would add a custom vocabulary that every binding must learn, and C
alone is too coarse inside the engine and leans on `std::expected`, which ADR-0013 has not allowed yet. Every
option needs a stable public catalogue, so the catalogue is decided on its own.

### Representation

- Fallible functions return `absl::Status` or `absl::StatusOr<T>` with the canonical code that fits (for example
  `kUnavailable` for a transient network failure).
- An `ErrorDetail` is attached as the status payload: catalogue code, category, severity, retryable flag, a
  context map of redacted key/value pairs, and an optional cause. Typed helpers write and read it, so no caller
  handles the raw payload.

### Public catalogue

- Codes are grouped in ranges by category: 1xxx network, 2xxx manifest, 3xxx demux and media, 4xxx decode, 5xxx
  DRM, 6xxx text, 7xxx ads, 8xxx player, API and settings.
- The catalogue is generated from one source, like the settings schema
  ([ADR-0017](0017-settings-schema-and-configuration.md)), into C++, the SDK enums and the documentation. Each code
  declares its default severity, whether it is retryable and its `MediaError` code.
- Once released, a code never changes meaning and is never reused. New codes may be added at any time.

### Severity

- **Recoverable:** handled internally, such as a retry that succeeded; reported only to telemetry (D-30, #30).
- **Degraded:** playback continues with reduced quality or features, such as an excluded rendition, a fallback
  to a software decoder or disabled captions; the app receives a warning event.
- **Fatal:** playback stops, and the app receives a `MediaError`.

### Retries

| Failure | Default |
|---------|---------|
| Timeouts, connection resets, transient DNS failures, HTTP 408 and 5xx | Retry |
| HTTP 429, or 503 with `Retry-After` | Retry after the requested delay, if it does not exceed the maximum backoff; otherwise fail over at once |
| HTTP 404 or 410 for a live segment that has fallen behind the window | Resynchronise to the live window (#127) |
| HTTP 404 near the live edge | Retry, since the segment may not be published yet |
| Other HTTP 404 | Fail over |
| HTTP 401 and 403 | Never retry the same URL; fail over to another pathway, then fatal |
| Other HTTP 4xx | Not retried |

Retries use exponential backoff from an initial delay up to a maximum, with random jitter so many players do not
retry in lockstep. A `Retry-After` longer than the maximum backoff never holds playback: the request moves to the
next pathway instead.

### Recovery ladder

One component owns escalation:

1. retry with jittered exponential backoff;
2. fail over to another pathway or CDN (HLS redundant variants and Content Steering, DASH `BaseURL` and Content
   Steering);
3. exclude the rendition and let ABR choose another;
4. switch the decoder or recreate the port player (D-06, #6);
5. fatal error.

An excluded rendition stays excluded for 60 seconds by default, Media3's default. A failed pathway stays excluded
until the next Content Steering refresh, or for 300 seconds without steering. A separate stall watchdog covers the
case where nothing failed but playback is not advancing, and jumps small buffer gaps. Every attempt is emitted as
a non-fatal error event for telemetry.

### Port errors

Ports report errors with a category and a hint: transient, recreate or fatal. A recreate hint, such as a decoder
lost after the app returns from the background (Starboard's `kSbPlayerErrorCapabilityChanged`), enters the ladder
at the fourth step: Umbra rebuilds the port player and resumes from the last position, and the error becomes fatal
only if rebuilding keeps failing.

### `MediaError`

`MediaError.code` and `MediaError.message` follow HTML. The message starts with the catalogue code, for example
`ALUMBRA-4003: Video decoder failed to initialize (avc1.64001f)`. An Alumbra `detail` field alongside them exposes
the catalogue code, category, severity, retryable flag and redacted context, so apps never parse strings. D-03
(#3) fixes the exact public shape.

### Redaction

By default, logs, telemetry and error messages never contain URL query strings, fragments or user information,
`Authorization`, `Cookie`, `Set-Cookie` or configured token headers, or licence request and response bodies. An
integrator can opt in to full URLs for debugging. A CI test passes known secrets through the logging and error
paths and asserts they never appear in the output; D-30 (#30) shares it.

### Settings

The retry keys are the ones #75 defines: `network.retry.max_attempts` (default 3),
`network.retry.initial_backoff_ms` (default 500) and `network.retry.max_backoff_ms` (default 8000). The other
provisional keys are `abr.rendition_exclusion_ms`, `network.pathway_exclusion_ms`, `playback.stall.timeout_ms` and
`playback.gap_jump.max_gap_ms`. They enter the schema of ADR-0017 through #53.

## Consequences

**Positive**
- Errors are cheap to pass across sequences, the port boundary and bindings, using Google's own type.
- Apps, telemetry and support share precise, stable codes, while web code that only knows `MediaError` keeps
  working.
- Recovery is predictable and lives in one component, and apps hear about quality drops without treating them as
  failures.
- Secrets stay out of logs by default, enforced by a test.

**Negative / costs**
- Reading an `ErrorDetail` needs helpers, because status payloads are untyped.
- The catalogue is a public contract: codes cannot be renumbered or reused once released.
- `MediaError.detail` is an extension beyond the HTML specification.

**Follow-ups**
- #49 implements `ErrorDetail`, its helpers and the catalogue generator.
- #96 maps catalogue codes to events and `MediaError`; D-03 (#3) fixes the public shape.
- #127 implements retries, failover and exclusion in the recovery ladder.
- D-06 (#6) defines decoder fallback for the fourth step; D-30 (#30) shares the redaction test.

## References

- [D-09 discussion](https://github.com/LuisAhumadaMartens/alumbra/issues/9)
- [Abseil: Status guide](https://abseil.io/docs/cpp/guides/status)
- [Chromium: media/base/status.h (TypedStatus)](https://chromium.googlesource.com/chromium/src/+/main/media/base/status.h)
- [Media3: PlaybackException](https://developer.android.com/reference/androidx/media3/common/PlaybackException)
- [Shaka Player: shaka.util.Error](https://shaka-player-demo.appspot.com/docs/api/shaka.util.Error.html)
- [HTML: MediaError](https://html.spec.whatwg.org/multipage/media.html#mediaerror)
- [HLS: Content Steering](https://developer.apple.com/streaming/HLSContentSteeringSpecification.pdf)
- [RFC 9110: 429 and Retry-After](https://www.rfc-editor.org/rfc/rfc9110#field.retry-after)
- [Cobalt: starboard/player.h](https://github.com/youtube/cobalt/blob/main/starboard/player.h)
