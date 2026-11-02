# 0033 — Ship a hybrid ABR policy over a dual-EWMA estimator, with caps applied first

- **Status:** Accepted
- **Date:** 2026-11-01
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #11

## Context

ABR decides startup quality, switches and abandonment, so it largely decides how playback feels. D-11 blocks #119,
#120 and #220. #122 provides the simulation harness that evaluates the result. A comment on the issue adds bounded
CMSD hints (estimated throughput and maximum suggested bitrate) that must never override safety or user bounds.

Accepted decisions already shape it:

- [ADR-0026](0026-network-stack-and-http-transport.md) says web timing is coarse, because `fetch` gives no per-chunk
  arrival times.
- [ADR-0029](0029-decoder-selection-and-fallback.md) asks ABR to accept a resolution cap when sustained late
  decode drops occur.
- [ADR-0032](0032-unified-presentation-model.md) lists the valid audio and video combinations.
- [ADR-0023](0023-public-api-surface-and-media-error.md) fixes the public `abr()` accessor (caps and manual
  selection).
- [ADR-0017](0017-settings-schema-and-configuration.md) puts the unit last in a key.
- [ADR-0015](0015-threading-and-task-runners.md) provides the injected `TickClock`, which makes ABR a pure function
  of its inputs.

## Decision drivers

- Stable steady-state quality, and a fast, safe start.
- Rules that explain themselves, so the stats overlay (D-30) can show a reason for each switch.
- No frame-rate flips that cause visible cadence changes.
- Deterministic behaviour, so #122 can replay traces.
- A small public surface now, with room to open it later.

## Considered options

1. **Hybrid** — throughput rules at low buffer, BOLA when the buffer is healthy, plus insufficient-buffer and
   abandonment rules (dash.js `DYNAMIC` style).
2. **Throughput first** — dual EWMA with safety, insufficient-buffer and abandonment rules only. BOLA later.

## Decision

Chosen option: **Hybrid**, over a pluggable estimator. Composable rules vote with a reason string. The extra tuning
surface is what #122 exists to evaluate.

### Estimator

- Dual EWMA with a separate time-to-first-byte (TTFB) estimate.
- Use per-chunk arrival times where the transport provides them, and exclude idle gaps. Low-latency streams would
  otherwise estimate the encoding bitrate.
- On web `fetch`, fall back to whole-request timing.
- The estimator is replaceable behind an internal interface.

### Caps before rules

ABR first removes renditions that exceed:

- decoder capability (D-06),
- the viewport times device pixel ratio,
- `abr.max_height` and `abr.max_bitrate_bps`,
- data saver,
- CMSD hints, which can only lower the choice,
- the ADR-0029 drop trigger.

Frame rate is a dimension. ABR does not switch across frame rates unless forced, and never selects a rendition the
display and decoder cannot present smoothly.

### Startup and abandonment

- Start from `abr.default_bandwidth_bps` or an app-supplied persisted estimate. Pick the highest rung that fits at a
  conservative fraction, then upswitch quickly.
- Abort an in-flight segment only if its projected completion exceeds the buffered time and a lower rung would
  finish sooner.
- Audio ABR is independent for DASH and coupled by group for HLS, as the model in ADR-0032 states.

### Custom policies and simulation

- A C++ `AbrPolicy` interface exists internally. The public API stays the `abr()` accessor. A public custom-policy
  hook is deferred.
- #122 scores policies with a linear QoE model (bitrate utility, rebuffer and switch penalties, as in the MPC
  literature) over public throughput traces. Which trace corpus to use is unverified and is chosen in #122.

### Settings

Keys follow ADR-0017, with the unit last. The issue's `abr.estimator.*_half_life_s` and `abr.switch_buffer_s` become
`abr.estimator.fast_half_life_ms`, `abr.estimator.slow_half_life_ms` and `abr.switch_buffer_ms`. The injected clock
in the issue is ADR-0015's `TickClock`.

## Consequences

**Positive**
- Stable in steady state, and aware of buffer health.
- Every switch has a recorded reason.
- The ADR-0029 cap and CMSD hints fit as inputs, not special cases.

**Negative / costs**
- More tuning surface than throughput-only ABR.
- Web estimates stay coarse until a better timing source exists.

**Follow-ups**
- #119 builds the estimator and #120 the rule engine, with the reason strings.
- #121 implements capability and viewport filtering as the first stage.
- #122 evaluates the hybrid against throughput-only on traces and tunes the constants.
- #220 (M8) adapts ABR for low latency.
- Add the `abr.*` keys above to the settings schema (#119, #120).

## Pros and cons of the options

### Hybrid
- Good, because it is the most stable, and it reacts to buffer health.
- Bad, because there are more constants to tune.

### Throughput first
- Good, because it is fastest to build and easiest to debug.
- Bad, because it is noisier in steady state and blind to buffer health.

## References

- [dash.js ABR documentation](https://dashif.org/dash.js/pages/usage/abr/)
- [BOLA: Near-optimal bitrate adaptation for online videos](https://arxiv.org/abs/1601.06748)
- [Model predictive control for video streaming (Yin et al., SIGCOMM 2015)](https://dl.acm.org/doi/10.1145/2785956.2787486)
- [CTA-5006: Common Media Server Data (CMSD)](https://shop.cta.tech/products/common-media-server-data-cta-5006)
- [ADR-0015](0015-threading-and-task-runners.md), [ADR-0017](0017-settings-schema-and-configuration.md),
  [ADR-0026](0026-network-stack-and-http-transport.md), [ADR-0029](0029-decoder-selection-and-fallback.md)
