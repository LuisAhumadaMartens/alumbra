# 0023 — Expose a strict HTMLMediaElement subset, checked against browsers, with attachable modules

- **Status:** Accepted
- **Date:** 2026-11-01
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #3

## Context

[ADR-0002](0002-portable-engine-porting-layer-html5-api.md) gives Umbra a public API shaped like the WHATWG
`HTMLMediaElement` on every platform; issue #3 cites ADR-0001 for this by mistake. D-03 settles what that means in
practice: how strictly the C++ API follows the HTML algorithms, which members ship first, what `play()` returns in
C++, how integrators reach features that HTML lacks, and how the API is projected into TypeScript, Kotlin and Swift.
It blocks the playback state machine (#89), the C++ API (#95), events and `MediaError` mapping (#96) and track
selection (#125), and the headless UI state machine (D-28, #28) builds on its event model. `readyState` and event
order matter most, because UI state machines and ad SDKs depend on them.

The HTML model is precise. `readyState` runs from `HAVE_NOTHING` (0) to `HAVE_ENOUGH_DATA` (4), and its
transitions fire `loadedmetadata`, `loadeddata`, `canplay`, `canplaythrough` and `waiting`. `play()` returns a
promise that resolves when playback starts and rejects with `NotAllowedError`, `NotSupportedError` or `AbortError`.
`MediaError` has four codes and a free-form message. C++ has no promises, and Alumbra has features the element
lacks: ABR control, statistics, settings, DRM configuration, ads and UI.

Accepted decisions already settle several parts, and this ADR does not reopen them:

- **Event delivery** ([ADR-0015](0015-threading-and-task-runners.md#embedder-contract)). Events go into a
  per-player queue that the embedder drains after a notify hook ("notify, then pull"). Re-entrancy follows HTML.
  Attributes that HTML updates synchronously, such as `paused` and `currentTime`, change immediately. `timeupdate`
  fires at most every 250 ms by default (`player.events.timeupdate_interval_ms`).
- **`MediaError`** ([ADR-0018](0018-error-model-and-recovery.md#mediaerror)). `code` and `message` follow HTML,
  and the message starts with the catalogue code (`ALUMBRA-NNNN`). A `detail` field carries the catalogue code,
  category, severity, retryable flag and redacted context. Fatal errors set `MediaError`, degraded errors raise a
  warning event, and recoverable errors go only to telemetry.
- **Installed headers** ([architecture overview](../architecture/overview.md#public-headers),
  [ADR-0013](0013-cpp-language-standard.md)). They use only the C++20 standard library and other installed
  Alumbra headers, with no Abseil, and the installed-consumer test enforces this.
- **Opt-in modules** ([ADR-0012](0012-repository-topology-and-packaging.md)). Ads and UI ship as opt-in
  packages, so integrators get no ad or UI code unless they ask for it. Their API must therefore be absent, not
  merely inert, when they are not linked.
- **API documentation** ([ADR-0022](0022-documentation-site-and-api-reference.md)). It lives in sidecar files,
  never in source comments.

## Decision drivers

- What web developers know about `HTMLMediaElement` holds in every language, including at the edges.
- Conformance is checked against an external oracle, browsers and web-platform-tests, rather than claimed.
- Installed headers stay within the C++20 standard library.
- Opt-in modules are absent by construction when not linked, and features every build has need no runtime lookup.
- Bindings feel native: promises and `EventTarget` in TypeScript, `suspend` functions and `Flow` in Kotlin,
  `async throws` and `AsyncSequence` in Swift.
- Integrators can tell what is safe to use before 1.0, without the API being frozen early.
- The scope of M1 (First Light) stays small enough to finish.

## Considered options

The definition has eight questions. The issue's option C, a native-idiomatic core with an HTML facade only in the
TypeScript SDK, contradicts ADR-0002 and is not considered.

Fidelity to HTML:

1. **A — Strict subset, rest absent.** Every exposed member follows the HTML algorithms; members not built yet are
   absent.
2. **B — Shaped but simplified.** HTML names and events, with simplified semantics and documented deviations.
3. **C — Strict core, labelled deviations.** States, events, `play()` and seeking are strict; other members may ship
   with a documented deviation if they are marked experimental.

The v1 subset and its conformance table:

1. **A — M1 core with a generated table**, checked by ported web-platform-tests and event traces recorded in Chrome.
2. **B — Exit-criteria minimum** (`play`, `pause`, `currentTime`, `duration`, `readyState`, events and
   `MediaError`) with a hand-written table.
3. **C — Near-full element**, adding the track lists, `played`, `addTextTrack()` and `getStartDate()` in M1.

`play()` in C++:

1. **A — Callback with an Alumbra result:** `void Play(PlayCallback done)` with an Alumbra-owned C++20 `PlayResult`.
2. **B — Future-like request handle** that can be queried or given a continuation.
3. **C — Both**, a callback overload and a handle.

Features that HTML lacks:

1. **A — Core accessors, attachable modules:** `abr()`, `stats()`, `settings()` and `drm()` on the player; opt-in
   modules attach from their own packages.
2. **B — Typed extension registry:** every feature is reached through `GetExtension<T>()`, which returns null when
   the module is not linked.
3. **C — Wrapper players per bundle**, such as an `AdsPlayer` that composes `Player`.

`readyState` with several tracks and during ad breaks:

1. **A — Minimum over selected tracks**; during client-side ad breaks the element keeps reporting the content.
2. **B — The video track drives it**; audio-only streams use audio.
3. **C — Reflect whatever plays**, including the ad media during client-side breaks.

The `progress` cadence and live `duration`, `seekable` and `getStartDate()`:

1. **A — Follow HTML exactly.**
2. **B — Simplify:** `progress` fires on the `timeupdate` tick, and live `duration` is the end of the seekable
   range.

Naming and stability before 1.0:

1. **A — Mechanical naming map, with `stable` and `experimental` labels.**
2. **B — Freeze at first publish** (M4): additive changes only, with deprecation aliases kept for one minor release.
3. **C — No guarantees until 1.0.**

Platform player adapters:

1. **A — An optional Media3 adapter on Android, and Now Playing and remote-command helpers on Apple platforms.**
2. **B — A Media3 `Player` adapter and an AVPlayer-like Swift layer, both first-class parts of the core SDKs.**
3. **C — Leave both to integrators.**

## Decision

Chosen option: **A for all eight questions.** Strictness costs little when members that are not built yet are
simply absent, browsers become the test oracle, and each choice is the smallest one consistent with ADR-0012,
ADR-0013, ADR-0015 and ADR-0018. Type and namespace names below are illustrative; #95 fixes them.

### Fidelity

- Every member Umbra exposes follows the HTML algorithms normatively: resource selection, the load algorithm,
  `readyState` transitions and the events they fire, event order, seeking and the list of pending play promises.
- A member that is not built yet is absent from the API. It is never stubbed, approximated or shipped with a known
  deviation, so adding it later is an additive change.
- Extensions beyond HTML, such as `MediaError.detail` and the accessors below, never change what an HTML member
  means.

### The v1 subset

The first release covers these members, with the events their algorithms fire:

- `src`, `load()`, `canPlayType()` and `networkState`;
- `play()`, `pause()`, `paused`, `ended`, `autoplay` and `loop`;
- `currentTime`, `fastSeek()`, `seeking`, `duration`, `buffered` and `seekable`;
- `readyState` and `preload`;
- `playbackRate`, `defaultPlaybackRate`, `preservesPitch`, `volume` and `muted`;
- `error`.

These are the members the desktop UI and the conformance suite (#100) need anyway. Everything else is absent until a
later milestone adds it, including `played`, the track lists (#125, #129), `addTextTrack()`, `getStartDate()`,
`srcObject`, `<source>` children and `crossOrigin`, which settings cover. `controls` stays absent for good, because
UI belongs to Antumbra ([ADR-0007](0007-ui-architecture.md)).

### Conformance table

- One source file lists every HTML member and event and every Alumbra extension. A generator writes the table into
  `docs/reference/`, as `tools/errors/generate.py` writes the error catalogue, and nobody edits the page by hand.
- Each row names the member, event or extension, its C++ and binding names, whether it is implemented (and since
  which milestone) or absent, its stability label and the tests that cover it.
- The generator fails the build when a public member has no row, or when a row marked implemented names no
  covering test.
- Three kinds of evidence keep the table honest:
  - web-platform-tests cases from `html/semantics/embedded-content/media-elements`, ported to GoogleTest and run
    against the fake port on virtual time and against the desktop port headless (#100). A mapping file records
    each test as adopted, adapted or not applicable, with a reason.
  - Event traces recorded in Chrome for the same scenarios (load, play, pause, seek, end, error and stall). They are
    compared by event order and by the attribute values seen at each event, not by timing. Each trace records the
    Chrome version and the fixture it played, as [ADR-0021](0021-testing-strategy.md) requires of test inputs.
  - From M4, the web-platform-tests subset, run against the TypeScript binding in real browsers (#153, #156).
- Reference documentation for each member lives in sidecar files under `docs/api/cpp/` (#63), as ADR-0022
  requires.

### `play()` and `PlayResult`

- `void Play(PlayCallback done)` runs `done` exactly once, on the embedder's runner, through ADR-0015's event
  queue. The result is therefore ordered against `play`, `playing` and `pause` as HTML orders the promise.
- `PlayResult` is an Alumbra-owned C++20 type: a code (`kOk`, `kNotAllowed`, `kNotSupported` or `kAbort`) and an
  optional `ErrorDetail`, the public type of ADR-0018 (#49). The codes match the outcomes of HTML's `play()`:
  resolved, `NotAllowedError`, `NotSupportedError` and `AbortError`.
- `kAbort` reports a pending play interrupted by `pause()` or `load()`. `kNotAllowed` reports a platform policy that
  refuses playback, such as a browser's autoplay rules. `kNotSupported` carries the detail of the `MediaError` that
  caused it.
- `PlayCallback` is `std::function` or a small Alumbra-owned callable; #95 chooses. It cannot be
  `absl::AnyInvocable`, because installed headers exclude Abseil, or `std::move_only_function`, which ADR-0013 bans.
- If the player is destroyed before the result is delivered, the callback is destroyed without running, as
  ADR-0015 drops a destroyed player's remaining events.
- Bindings map the callback to a `Promise` in TypeScript, a `suspend fun` in Kotlin and an `async throws` function
  in Swift. D-18 (#18) designs their error types.

### `MediaError`

- `error()` is empty until a fatal error occurs, as HTML's `error` is null, and `load()` clears it.
- `MediaError` has `code()` (1 to 4, as in HTML), `message()`, which starts with `ALUMBRA-NNNN`, and `detail()`,
  which returns ADR-0018's `ErrorDetail`. Each catalogue code declares its `MediaError` code; #96 implements the
  mapping.
- Degraded errors reach the app as a typed warning event that carries an `ErrorDetail`, through the subscription
  mechanism below. Recoverable errors are not app events: they go only to telemetry
  ([ADR-0020](0020-observability-logging-tracing-qoe-metrics.md)).

### Features that HTML lacks

- Features every build has are accessors on the player:
  - `abr()`: ABR control, including the quality selection of #125;
  - `stats()`: the statistics snapshot of ADR-0020 (#126);
  - `settings()`: the per-player settings layer of [ADR-0017](0017-settings-schema-and-configuration.md);
  - `drm()`: DRM configuration that EME does not cover. The EME-shaped members of #235 follow the strict HTML
    rule.

  Each accessor appears when its feature is built and never returns null.
- Opt-in modules, ads and UI, attach from their own packages, for example `AdController::Attach(player)` from the
  ads package. Core headers never mention them, so a module that is not linked is absent by construction, with no
  registry.
- One subscription mechanism serves the element, every accessor and every module: a typed `AddEventListener` that
  returns an RAII subscription, built on ADR-0015's queue. Each event has its own C++ type, and all events share the
  one queue, so their relative order is defined.

### `readyState` across tracks and ad breaks

- `buffered` is the intersection of the selected tracks' buffered ranges, and `readyState` is the lowest level
  among the enabled audio tracks and the selected video track, as Media Source Extensions computes them. An audio
  underrun therefore lowers `readyState` and fires `waiting`, as a video underrun does.
- During a client-side ad break the element keeps reporting the content: its position, state and events describe
  the content, not the ad. Ad playback state lives in the ads module's controller.
- Server-side inserted ads are part of the content stream, so the element treats them as ordinary content; the ads
  module maps their timeline (D-26, #26).

### Live streams and `progress`

- `progress` fires about every 350 ms (±200 ms) while media data is being fetched, as the HTML resource fetch
  algorithm specifies. `timeupdate` keeps the cadence ADR-0015 set.
- A live stream with no announced end reports a `duration` of +Infinity. Its `seekable` range is the DVR window and
  moves with it.
- `getStartDate()` derives the timeline offset from `EXT-X-PROGRAM-DATE-TIME` in HLS or `availabilityStartTime` in
  DASH, and returns NaN when neither exists.
- These rules apply from the first live sources (#107, #116), and `getStartDate()` joins the subset with them.
  D-13 (#13) and #218 build the live edge and seek-to-live on them.

### Naming and stability

- One fixed rule maps the Google-style C++ names to the bindings: `Play()` becomes `play()`, `current_time()`
  becomes `currentTime`, and `set_current_time()` becomes a property setter. D-18's binding generator applies the
  rule and extends it, just as mechanically, to constants and event names.
- Bindings expose events as `EventTarget` events in TypeScript, a `Flow` in Kotlin and an `AsyncSequence` in Swift.
- Before 1.0, every public member, event and extension carries a `stable` or `experimental` label in the
  conformance table, as settings ([ADR-0017](0017-settings-schema-and-configuration.md)) and design tokens
  ([ADR-0019](0019-design-tokens.md)) do. An `experimental` member may change in any release. A `stable` member is
  not expected to change; if one must change before 1.0, the changelog lists it with every other breaking change.
- Version numbering and the policy after 1.0 belong to D-34 (#34).

### Platform adapters

- On Android, an optional Maven module provides a Media3 `SimpleBasePlayer` adapter, so MediaSession, system media
  controls and Cast work with Alumbra (M6; #194 builds the MediaSession part). The core Kotlin SDK does not depend
  on Media3.
- On Apple platforms, Now Playing (`MPNowPlayingInfoCenter`) and remote-command (`MPRemoteCommandCenter`) helpers
  take the place of an AVPlayer-like layer (M7, #212).
- D-18 (#18) settles the details of both.

### Corrections to the issue

The proposal in #3 is corrected in five places to match accepted decisions:

1. The HTML-shaped API comes from ADR-0002, not ADR-0001.
2. `Play()` does not report an `absl::Status`: installed headers may not include Abseil, and the installed-consumer
   test rejects any that do. `std::expected` is ruled out too, because it is C++23 and public headers stay at C++20
   (ADR-0013).
3. Recoverable errors are not reported as extension events. ADR-0018 sends them only to telemetry and raises a
   warning event only for degraded errors.
4. Events are not posted as separate tasks on the embedder's runner. ADR-0015's notify-then-pull queue carries
   them, and `AddEventListener` sits on top of that queue.
5. API and conformance documentation goes in sidecar files (ADR-0022), not in source comments.

## Consequences

**Positive**
- Web developers' expectations hold on every platform, and browsers serve as an executable oracle.
- Conformance claims are generated and backed by tests, so the table cannot drift from the code.
- The C++ API needs only C++20 and no future machinery, and each binding maps `PlayResult` to its native async idiom.
- Opt-in modules are absent by construction, and features every build has need no nullable lookup.
- UI, analytics and ad SDKs see one content timeline during ad breaks, and audio underruns surface as `waiting`.
- Live streams behave as they do in browsers and in Safari's native HLS.
- Integrators can see what is safe to use without an early freeze.
- Android gets system media integration without growing the core.

**Negative / costs**
- Strictness is the most expensive option, mostly in `readyState` and event order, and `readyState` needs
  buffering accounting per track.
- Porting web-platform-tests cases and recording Chrome traces is real work, and traces must be re-recorded and
  reviewed when Chrome's behaviour changes.
- `Play()` uses a callback in C++.
- Integrators learn two access patterns: accessors for core features and `Attach` for modules.
- UIs must handle a `duration` of +Infinity.
- Stability labels need upkeep.
- The Media3 adapter is one more module, and the Apple helpers need maintenance.
- Members outside the v1 subset, such as `played` and the track lists, are unavailable until later milestones add
  them.

**Follow-ups**
- #89 builds the playback state machine on the HTML algorithms, the list of pending play promises and the
  per-track `readyState` rule.
- #95 implements the v1 members, `Play()` and `PlayResult`, `MediaError`, each accessor as its feature lands, and
  the conformance table's source and generator. Its scope drops `played` and `getVideoPlaybackQuality()`, which
  stay absent until a later milestone adds them; dropped frames are available from `stats()` meanwhile.
- #96 implements the events and their order on ADR-0015's queue, the typed subscription, the warning event, the
  `progress` cadence and the mapping from catalogue codes to `MediaError`.
- #100 ports the web-platform-tests cases, adds the Chrome trace comparisons and supplies the table's covering
  tests; #156 runs the subset in browsers from M4.
- #125 adds `audioTracks` and `videoTracks` under the strict rule, and quality selection under `abr()`.
- D-28 (#28) builds the headless UI state machine on this event model, including a `duration` of +Infinity and the
  DVR window.
- D-18 (#18) implements the naming map, the async and event mappings, the Media3 adapter and the Apple helpers.
- D-13 (#13) and #218 build the live model on the live rules above.
- #63 documents the public API in sidecar files. D-34 (#34) decides version numbering and the policy after 1.0.

## Pros and cons of the options

### Fidelity to HTML

- **A. Strict subset, rest absent.** Good, because web developers' intuition holds and browsers serve as a test
  oracle. Bad, because it is the most work, mostly in `readyState` and event order.
- **B. Shaped but simplified.** Good, because it is faster to build. Bad, because it breaks at exactly the edges
  that UI and ad SDKs rely on, and it is hard to test against an oracle.
- **C. Strict core, labelled deviations.** Good, because more members ship sooner. Bad, because labelled deviations
  tend to become permanent.

### The v1 subset and its conformance table

- **A. M1 core with a generated table.** Good, because conformance is checked rather than claimed, and the members
  are the ones the desktop UI and #100 need anyway. Bad, because porting the tests and recording the traces is real
  work.
- **B. Exit-criteria minimum.** Good, because it is quickest. Bad, because D-28 needs `paused`, `ended`, `seeking`
  and `buffered` anyway, and a hand-written table drifts.
- **C. Near-full element.** Good, because fewer members arrive later. Bad, because the track lists depend on D-15
  (M3) and live behaviour on D-13, which is scope creep for First Light.

### `play()` in C++

- **A. Callback with an Alumbra result.** Good, because it maps one to one onto the HTML promise and needs no future
  machinery. Bad, because callbacks compose less well in C++.
- **B. Future-like request handle.** Good, because it composes and callers can ignore it. Bad, because Alumbra
  would own a future type with its own lifetime and threading rules.
- **C. Both.** Good, because it is flexible. Bad, because one method gets two APIs to test and document.

### Features that HTML lacks

- **A. Core accessors, attachable modules.** Good, because modules are absent by construction, with no registry and
  no runtime indirection for the features every build has. Bad, because integrators learn two access patterns.
- **B. Typed extension registry.** Good, because there is one uniform pattern. Bad, because even features that
  always exist become nullable runtime lookups.
- **C. Wrapper players per bundle.** Good, because ownership is clear. Bad, because every combination, such as ads
  plus UI, needs its own wrapper type.

### `readyState` with several tracks and during ad breaks

- **A. Minimum over selected tracks.** Good, because it matches Media Source Extensions and IMA's model of a
  separate ad element. Bad, because it needs buffering accounting per track.
- **B. The video track drives it.** Good, because it is simpler. Bad, because audio underruns never surface as
  `waiting`.
- **C. Reflect whatever plays.** Good, because it matches single-element web players. Bad, because content events
  fire for ads, which confuses UI and analytics.

### The `progress` cadence and live details

- **A. Follow HTML exactly.** Good, because it matches browsers and Safari's native HLS at no extra cost. Bad,
  because UIs must handle +Infinity.
- **B. Simplify.** Good, because naive UIs get simpler arithmetic. Bad, because it diverges from HTML, and "is live"
  becomes ambiguous.

### Naming and stability before 1.0

- **A. Mechanical naming map, stability labels.** Good, because it is consistent with settings and design tokens,
  and it shows what is safe to use without freezing the API. Bad, because the labels need upkeep.
- **B. Freeze at first publish.** Good, because integrators can trust every published member. Bad, because it
  freezes the API while ports are still arriving.
- **C. No guarantees until 1.0.** Good, because it leaves complete freedom. Bad, because early adopters cannot tell
  what is safe to use.

### Platform player adapters

- **A. Optional Media3 adapter and Apple helpers.** Good, because Android gets the system integration it needs
  without growing the core. Bad, because it adds one more module.
- **B. Both first-class in the core SDKs.** Good, because mobile developers find familiar APIs. Bad, because it
  adds two API surfaces, designed before those ports exist.
- **C. Leave both to integrators.** Good, because it is the least work. Bad, because every Android app has to
  rebuild its MediaSession integration.

## References

- [D-03 discussion](https://github.com/LuisAhumadaMartens/alumbra/issues/3)
- [HTML: media elements](https://html.spec.whatwg.org/multipage/media.html) ·
  [ready states](https://html.spec.whatwg.org/multipage/media.html#ready-states) ·
  [playing the media resource](https://html.spec.whatwg.org/multipage/media.html#playing-the-media-resource) ·
  [resource fetch algorithm](https://html.spec.whatwg.org/multipage/media.html#concept-media-load-resource) ·
  [`getStartDate()`](https://html.spec.whatwg.org/multipage/media.html#dom-media-getstartdate) ·
  [error codes](https://html.spec.whatwg.org/multipage/media.html#error-codes) ·
  [event summary](https://html.spec.whatwg.org/multipage/media.html#mediaevents)
- [Media Source Extensions: HTMLMediaElement extensions](https://www.w3.org/TR/media-source-2/#htmlmediaelement-extensions)
- [web-platform-tests: media-elements](https://github.com/web-platform-tests/wpt/tree/master/html/semantics/embedded-content/media-elements)
- [RFC 8216: `EXT-X-PROGRAM-DATE-TIME`](https://datatracker.ietf.org/doc/html/rfc8216#section-4.3.2.6)
- [IMA SDK for HTML5: client-side ads](https://developers.google.com/interactive-media-ads/docs/sdks/html5/client-side)
- [Media3: SimpleBasePlayer](https://developer.android.com/reference/androidx/media3/common/SimpleBasePlayer)
- [Apple: MPNowPlayingInfoCenter](https://developer.apple.com/documentation/mediaplayer/mpnowplayinginfocenter) ·
  [MPRemoteCommandCenter](https://developer.apple.com/documentation/mediaplayer/mpremotecommandcenter)
- [Google C++ Style Guide: naming](https://google.github.io/styleguide/cppguide.html#Naming)
- [ADR-0002](0002-portable-engine-porting-layer-html5-api.md), [ADR-0007](0007-ui-architecture.md),
  [ADR-0012](0012-repository-topology-and-packaging.md), [ADR-0013](0013-cpp-language-standard.md),
  [ADR-0015](0015-threading-and-task-runners.md), [ADR-0017](0017-settings-schema-and-configuration.md),
  [ADR-0018](0018-error-model-and-recovery.md), [ADR-0019](0019-design-tokens.md),
  [ADR-0020](0020-observability-logging-tracing-qoe-metrics.md), [ADR-0021](0021-testing-strategy.md),
  [ADR-0022](0022-documentation-site-and-api-reference.md)
