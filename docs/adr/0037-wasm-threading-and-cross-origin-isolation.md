# 0037 — Build the web engine twice, threaded when the page is cross-origin isolated and single-threaded otherwise

- **Status:** Accepted
- **Date:** 2026-11-01
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #17

## Context

Threaded WASM needs `SharedArrayBuffer`, which browsers expose only to cross-origin isolated pages. Isolation
breaks cross-origin ads, iframes and popups, and it does so worst on Safari. The IMA HTML5 SDK is an ad iframe
integration, so on most publisher sites a threads-only build would break ads. D-17 sets the web execution model and
blocks #143, #149 and #158, and #173 in M5.

[ADR-0015](0015-threading-and-task-runners.md) already requires that threads are an optimisation and never a
correctness requirement. Its lock-free audio ring has no web equivalent without `SharedArrayBuffer`. The maintainer
wants threads for speed: parallel demux and software decode matter on weak devices, and
[ADR-0036](0036-web-output-selection-and-ffmpeg-wasm-addon.md) puts software decode in the last compatibility layer.

## Decision drivers

- Speed on weak devices, for demux and for software decode in ADR-0036's layers 3 and 4.
- Ads and embeds keep working on sites that cannot isolate.
- Works without special headers.
- Threads stay an optimisation (ADR-0015), so both builds behave the same.
- A measured decision: the isolation experiment in #143 runs in full.

## Considered options

1. **Defer threads**: a single-threaded engine in a worker only, for M4 and 1.0.
2. **Dual build**: a single-threaded worker build, plus a pthreads build used when `crossOriginIsolated` is true.
3. **Threads only**, requiring isolation. The issue's option D, rejected as it would break IMA on most sites.

## Decision

Chosen option: **a dual build.** The threaded WASM engine runs whenever the page is cross-origin isolated. The
single-threaded worker build is the fallback wherever isolation would break IMA, iframes or popups. Threads are
never required, so the fallback stays permanently.

### Loader

- The loader checks `crossOriginIsolated` and a setting, then fetches one of the two binaries. Only one is
  downloaded.
- Both builds run the same engine code and the same ADR-0015 task runner. Threads add parallel demux and decode
  workers.
- The single-threaded build runs in a dedicated worker. The page's main thread owns the `<video>` and the DOM. A
  main-thread variant stays selectable for restricted webviews (`engine.use_worker`).
- Worker protocol: one message per event-queue drain (notify, then pull, ADR-0015), transferable buffers, and
  backpressure from pull flow control.
- MSE is fed from the worker through `MediaSourceHandle` where it exists (Chrome 108 and Safari 18; unverified).
  Elsewhere a main-thread proxy appends transferred segments.

### Audio

- When isolated, the lock-free audio ring uses `SharedArrayBuffer` into the AudioWorklet, as on native ports.
- When not isolated, PCM travels over a `MessagePort` to the AudioWorklet, with more buffering. Buffer sizes come
  from measurement in #143 and #149. This matters only on the WebCodecs path, because MSE audio is browser-managed.

### Reducing ad breakage

- Where the browser supports it, prefer `Cross-Origin-Embedder-Policy: credentialless`, and the
  `Document-Isolation-Policy` header, over `require-corp`, to reduce the loss of cross-origin resources.
  Support differs by browser and version, and which browsers support which policy is unverified. #143 verifies
  it and records the result in the compatibility matrix.
- Alumbra cannot set a page's headers. Isolation is the integrator's choice, and the player reports whether the
  page is isolated, and which build runs, in stats.
- A static host that cannot set response headers, such as GitHub Pages (unverified against current documentation),
  runs the single-threaded build unless #158 chooses a workaround.

### Memory

A fixed maximum per device profile applies. Failure to reserve memory yields `kResourceExhausted` and the
constrained profile of D-22 ([ADR-0039](0039-compatibility-matrix-and-support-tiers.md)).

### Experiment

The #143 experiment runs in full: long tasks, audio underruns and size on at least one phone and one TV, and the
isolation-by-IMA matrix on Chrome, Edge, Firefox and Safari on desktop, Safari on iPhone and iPad, Chrome on
Android and one TV browser. Its results may adjust the loader rules and the headers advice, but not the dual-build
decision.

### Settings

`engine.threading` (`auto`, `single`, `threaded`) and `engine.use_worker` (boolean), per ADR-0017, where `auto`
checks `crossOriginIsolated`.

## Consequences

**Positive**
- Integrators with isolation get parallel demux and decode, and a lock-free audio ring.
- Integrators with ads and iframes keep a working player with no headers.
- ADR-0036's software decode is feasible on weak devices where isolation is available.

**Negative / costs**
- Two binaries: doubled build and CI time, and doubled size budgets and test matrix.
- The two builds need parity tests, so a threading bug cannot hide in only one.
- The audio path differs by build and needs its own tuning.

**Follow-ups**
- #143 adds the threaded WASM preset next to the single-threaded one and runs the full isolation experiment.
- #145 sets size and startup budgets for both builds.
- #149 builds both audio paths (shared ring and `MessagePort`).
- #158 decides how the Pages demo is hosted for each build.
- #173 (M5) uses the single-threaded build as the IMA baseline.
- New task: CI parity tests that run the conformance set on both builds.
- Add `engine.threading` and `engine.use_worker` to the settings schema (#143).
- The threaded build is also the one the `alumbra-ffmpeg` add-on uses for threaded video decode (ADR-0036).

## Pros and cons of the options

### Defer threads
- Good, because it has the smallest build and test matrix and no ad risk.
- Bad, because there is no parallel demux or decode, and no lock-free audio ring.

### Dual build
- Good, because it serves both kinds of integrator, and speed is available where isolation is.
- Bad, because there are two binaries, and CI and size budgets double.

### Threads only
- Good, because there is one build.
- Bad, because it breaks IMA, iframes and popups on most sites, and fails ADR-0015's rule that threads are an
  optimisation.

## References

- [Cross-origin isolation guide (web.dev)](https://web.dev/articles/coop-coep)
- [MDN: SharedArrayBuffer security requirements](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/SharedArrayBuffer#security_requirements)
- [MDN: Cross-Origin-Embedder-Policy](https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Headers/Cross-Origin-Embedder-Policy)
- [Emscripten pthreads](https://emscripten.org/docs/porting/pthreads.html)
- [MediaSourceHandle](https://developer.mozilla.org/en-US/docs/Web/API/MediaSourceHandle)
- [IMA HTML5 SDK](https://developers.google.com/interactive-media-ads/docs/sdks/html5/client-side)
- [ADR-0015](0015-threading-and-task-runners.md), [ADR-0017](0017-settings-schema-and-configuration.md),
  [ADR-0024](0024-media-clock-and-av-sync.md), [ADR-0036](0036-web-output-selection-and-ffmpeg-wasm-addon.md)
