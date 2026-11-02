# 0034 — Set buffer goals in time, clamp them by byte budgets and keep pull-based pacing in the port

- **Status:** Accepted
- **Date:** 2026-11-01
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #12

## Context

D-12 fixes how far ahead and behind Umbra buffers, in time and in bytes, and how platform quotas and evictions
constrain that. A wrong default shows up as rebuffering on weak networks, or as memory pressure on 1 GB-class
devices. It blocks #103, #124 and #265.

The issue proposes that port-supplied budgets drive Umbra's goals. [ADR-0016](0016-penumbra-port-contract.md) says
the opposite about pacing: the port asks for data and owns decoder-queue pacing. This ADR resolves the overlap.

## Decision drivers

- Integrators think in seconds. Low-memory devices need bytes.
- One owner of pacing, not two.
- Budgets that reflect bitrate and frame rate, so 4K at 120 fps is not budgeted like 4K at 30.
- Quick development: no per-codec budget queries until a port needs them.
- Defaults that settings can change, so tuning does not need a new decision.

## Considered options

The issue's options:

1. **A — Time goals only.**
2. **B — Byte budgets only**, supplied by the port.
3. **C — Time goals clamped by byte budgets.**

## Decision

Chosen option: **C**, built on B. The definition has no maintainer questions, because the defaults below are tuning
that settings can adjust.

### Ownership

- Pull flow control stays. The port says it needs data and owns decoder-queue pacing.
- Umbra's goals only bound how far its loader and sample queue run ahead. Umbra never writes beyond a port grant.
- A port reports a byte budget only where it has one (the web MSE quota, TVs). Desktop uses a setting.
- The budget query returns one number per media type. Umbra estimates bytes from bitrate. Starboard's per-codec and
  per-resolution query is not adopted. Add granularity only if a TV port needs it.

### Starting defaults

These are starting values, unverified, and #122 and #124 tune them:

- forward goal 30 s
- back goal 10 s, measured from the last keyframe
- start threshold about 1 s
- rebuffer threshold larger, about 4 s
- lower byte caps on phones and TV browsers

### Pressure and edge cases

- On `QuotaExceededError`, evict behind the playhead first, then step the forward goal down (as Shaka does).
  `ManagedMediaSource` eviction schedules a refetch, and its `streaming` events gate the loader (M4).
- If the forward goal falls below one segment, raise it to one segment. If memory still does not allow that, cap ABR
  to a lower rung and report the degraded state ([ADR-0018](0018-error-model-and-recovery.md)).
- Budgets count bitrate and frame rate.
- Live: the forward goal is capped at the live-edge distance. DVR seeks use the manifest window, not the back
  buffer (D-13).
- Ad prefetch gets its own bounded budget (D-25).
- No pre-allocated buffer pool until a constrained port measures fragmentation.

### Settings

The issue's `buffer.max_bytes.video` and `.audio` break the ADR-0017 rule that the unit comes last. The keys are
`buffer.forward_goal_ms`, `buffer.back_goal_ms`, `buffer.start_threshold_ms`, `buffer.rebuffer_threshold_ms`,
`buffer.video_max_bytes` and `buffer.audio_max_bytes`.

## Consequences

**Positive**
- A single owner of pacing, so the port contract stays simple.
- Integrators set seconds, and devices stay safe through byte clamps.
- Pressure handling is defined before the web port meets real quotas.

**Negative / costs**
- Byte estimates from bitrate can be wrong for variable-bitrate content.
- One number per media type is coarse on TVs with codec-specific limits.
- The starting values are guesses until measured.

**Follow-ups**
- #103 implements the loader bounds, and #124 the buffer manager.
- #122 and #124 tune the defaults against traces and devices.
- #145 and #147 (M4) wire the MSE quota and `ManagedMediaSource` events.
- #265 (M11) covers memory-pressure testing on constrained ports.
- Add the `buffer.*` keys above to the settings schema (#124).

## Pros and cons of the options

### A — Time goals only
- Good, because they are easy to explain.
- Bad, because they give no protection on low-memory devices.

### B — Byte budgets only
- Good, because they protect memory directly.
- Bad, because integrators cannot reason about them, and bytes per second vary with the stream.

### C — Time goals clamped by byte budgets
- Good, because it serves both audiences.
- Bad, because there are two knobs and a bitrate-based estimate.

## References

- [Media Source Extensions: coded frame eviction](https://www.w3.org/TR/media-source-2/#sourcebuffer-coded-frame-eviction)
- [Managed Media Source](https://w3c.github.io/media-source/#managedmediasource-interface)
- [Shaka Player buffering configuration](https://shaka-player-demo.appspot.com/docs/api/tutorial-config.html)
- [Cobalt and Starboard research note](../research/cobalt-and-starboard.md), for the per-codec budget query not adopted
- [ADR-0016](0016-penumbra-port-contract.md), [ADR-0017](0017-settings-schema-and-configuration.md),
  [ADR-0018](0018-error-model-and-recovery.md)
