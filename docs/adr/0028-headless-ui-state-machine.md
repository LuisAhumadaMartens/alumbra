# 0028 — Build the headless UI as typed region reducers that publish frame-coalesced snapshots

- **Status:** Accepted
- **Date:** 2026-11-01
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #28

## Context

[ADR-0007](0007-ui-architecture.md) puts the player's UI behaviour in `umbra::ui`, a headless state machine in C++
that every platform shares. It decides what the chrome shows, what is enabled, what has focus and what is announced.
Thin renderers draw its output and forward input: the desktop renderer (#98 in M1, built with the technology D-19
(#19) chooses), the `<alumbra-player>` web component (#154 in M4), the Compose renderer (#192 in M6) and the SwiftUI
renderer (#210 in M7). D-28 settles the machine's shape: how it is implemented and authored, what its ViewModel
contains, how snapshots reach renderers, how its focus graph coexists with TV focus engines, its accessibility and
localisation model, and how apps customise the controls. It blocks #97, and the desktop renderer depends on #97.

The hard part is that several concerns change at the same time. Playback presentation (`idle`, `loading`,
`playing`, `paused`, `buffering`, `ended` and `error`) is derived from engine events. Chrome visibility runs
auto-hide timers and wakes on pointer, touch, keyboard and D-pad input. Menus nest. Ad mode (D-23, #23) reduces the
controls and can hand input to the ad provider. Fullscreen and picture-in-picture (D-24, #24) are modes. On TV,
focus moves through a graph. Each concern changes on its own, yet some combinations must never occur, such as a
settings menu that stays open during an ad.

The machine also decides how the controls feel. A play button that waits for an engine round trip, an auto-hide
timer that fires during a gesture, or a seek bar that steps every 250 ms on a 144 Hz display all make the player
feel slow, whatever the decoder does. The maintainer's priorities for this decision are smooth, responsive controls
and high refresh rates.

Accepted decisions already settle several parts, and this ADR does not reopen them:

- **Behaviour in C++** (ADR-0007). `umbra::ui` owns the D-pad focus graph and the accessibility labels. Focus is
  therefore not delegated to platform focus engines, and a statechart runtime in JavaScript is ruled out.
- **Events and threading** ([ADR-0015](0015-threading-and-task-runners.md)). Engine events reach the embedder's
  runner through a per-player queue. Attributes that HTML updates synchronously, such as `paused`, change
  immediately. Tests run on a `FakeTickClock`, without sleeping.
- **Event model** ([ADR-0023](0023-public-api-surface-and-media-error.md), D-03, #3). The machine consumes the
  HTML-shaped events and attributes of the v1 subset, including a `duration` of +Infinity for live streams and a
  `seekable` DVR window that moves. During client-side ad breaks the element keeps reporting the content, and ad
  state comes from the ads module. UI is an opt-in module that attaches to a player from its own package.
- **Golden ViewModel tests** ([ADR-0021](0021-testing-strategy.md)). The headless state machine's golden
  ViewModels belong to the unit and deterministic integration tier.
- **Styling** ([ADR-0019](0019-design-tokens.md)). Renderers style the UI only through semantic and component
  tokens, with the interaction states `hover`, `focus`, `pressed` and `disabled`, and a reduced-motion mode.
- **Settings** ([ADR-0017](0017-settings-schema-and-configuration.md)). Behaviour is configured through `ui.*` keys
  in the TOML schema, and a key that has a unit ends in it.
- **No comments** ([ADR-0022](0022-documentation-site-and-api-reference.md)). Alumbra-owned code has no comments,
  and API documentation lives in sidecar files.
- **Packaging** ([ADR-0012](0012-repository-topology-and-packaging.md)). The UI ships as opt-in packages that are
  built from one commit and versioned in lockstep.

## Decision drivers

- Identical behaviour on all four renderers, proved by golden tests in C++ rather than by reviewing each renderer.
- Responsive controls: the effect of an input is visible in the next display frame, without waiting for the engine.
- Smooth motion at any refresh rate, including 120 Hz, 144 Hz and above, without a snapshot for every frame.
- Invalid combinations of concerns cannot be represented where that is possible, and are checked everywhere else.
- Plain C++ that reads clearly without comments, compiles quickly and adds no dependency.
- Accessibility and localisation designed in from M1, so that no renderer has to retrofit them.
- Apps customise the controls without forking the machine and without breaking focus or accessibility.
- Freedom to change the design before 1.0.

## Considered options

The definition has eight questions. The issue's option C, a statechart authored in XState, conflicts with
ADR-0007's headless state machine in C++: it needs a JavaScript engine on native platforms or a code generator from
XState's JSON, and the declarative option of the second question already covers a generator. It is not considered
further. The issue's flat reducer with flags (its option A) is a weaker form of option C below, which replaces the
flags with typed states for each region.

Implementation of the machine:

1. **A — In-house statechart runtime** with SCXML semantics: hierarchy, parallel regions, entry and exit actions,
   guards, and timers as events.
2. **B — Boost.SML**, header-only and available in vcpkg as `bext-sml` under BSL-1.0, which ADR-0014 allows.
3. **C — Typed reducer over variants.** Each region is a `std::variant` of state structs inside one snapshot. One
   `Reduce(state, input)` per region, plus explicit cross-region rules, drives it.

Authoring:

1. **A — C++ source and a generated diagram:** a test-time tool exports the structure as a documentation diagram.
2. **B — Declarative source and a generator:** SCXML or JSON compiled to C++, as settings and tokens are.
3. **C — C++ only**, with no diagram.

ViewModel contents and versioning:

1. **A — Full model for in-repository renderers:** designed in full now, consumed only by renderers in this
   repository until 1.0, versioned with the lockstep release, with fields labelled `stable` or `experimental`.
2. **B — Public and separately versioned** for renderers outside the repository from now on.
3. **C — M1-minimal model:** play and pause, the seek bar, time, volume and fullscreen, extended later.

Snapshot delivery:

1. **A — Immutable snapshots coalesced to the display frame, with an extrapolated playhead.**
2. **B — A snapshot on every transition**, including each `timeupdate`.
3. **C — Diffs or patches.**

Focus and platform focus engines:

1. **A — The machine leads and renderers mirror it** on every platform.
2. **B — The machine leads except on tvOS**, where the platform focus engine moves focus and reports it back.

Accessibility:

1. **A — WAI-ARIA APG patterns with quiet announcements**, and a small M1 minimum.
2. **B — Full screen-reader support in M1.**
3. **C — Defer accessibility to M4.**

String catalogue:

1. **A — One catalogue with generated outputs** for every platform.
2. **B — Native catalogues per platform.**
3. **C — gettext `.po` files.**

Customisation:

1. **A — Hide, reorder and declare controls** through settings and a registration API.
2. **B — Renderer slots only**, holding native views the machine does not know about.
3. **C — Replace or fork the machine.**

## Decision

Chosen options: **C for the implementation, and A for the other seven questions.** A typed reducer is the quickest
to build and the most readable without comments, and the issue itself notes that a statechart can be a reducer over
a hierarchical type. Coalesced snapshots with an extrapolated playhead give smooth progress at any refresh rate
without flooding renderers. The other choices keep one model of behaviour, focus, accessibility and strings in C++,
where golden tests cover it for every renderer. Type, function and key names below are illustrative: #97 fixes the
type and function names, and #53 fixes the settings keys.

### Regions and reducers

- One immutable `UiState` holds the regions. Each region is a `std::variant` of state structs, so a region is never
  in two states at once, and data that belongs to one state, such as an open menu's path, exists only in that state.
  - **Playback:** `idle`, `loading`, `playing`, `paused`, `buffering`, `ended` and `error`, derived from ADR-0023's
    events and attributes.
  - **Chrome:** shown, auto-hiding with a deadline, pinned while a menu is open or the pointer is over the
    controls, and hidden.
  - **Menu:** closed, or open with a path through nested submenus and a highlighted item.
  - **Ad:** content, or an ad break with its input owner and skip state (D-23, #23).
  - **Presentation:** inline, fullscreen or picture-in-picture (D-24, #24).
  - **Focus:** none, or a target control, together with the control to restore when the chrome reappears.

  #97 may give a concern its own region, for example scrubbing, under the same rules.
- Inputs form one variant as well: intents from renderers, events from the engine and the ads module, and timer
  events.
- Each region has a pure `Reduce` function that takes the region's state, the input and a read-only view of the
  other regions and the settings, and returns the region's next state.
- Explicit cross-region rules run after the region reducers, in a fixed and documented order. They do the work that
  entry and exit actions do in a statechart. For example, entering an ad break closes the menu, ends a scrub and
  moves focus to the skip control; opening a menu pins the chrome; and a control that becomes hidden passes focus to
  its nearest visible neighbour.
- Reducers have no side effects. Each step returns the new state and a list of effects as values: calls to the
  player's API (ADR-0023), timer requests, announcements and notifications to the app. A thin driver applies them,
  and golden tests assert the effects as well as the snapshots.
- Timers are events. A reducer that needs a timeout records a deadline taken from the injected `TickClock`
  (ADR-0015) and emits a timer request. The driver posts a delayed task to the embedder's runner, and that task feeds
  back a timer input with a generation number; the machine ignores timers whose generation is stale. Auto-hide, the
  spinner delay and long presses therefore run on `FakeTickClock` in tests.
- One function checks the invariants after every step in debug builds and tests. The first invariants are:
  - no menu is open during an ad break;
  - a control that ad mode hides cannot be activated, by input or through the API;
  - the focus target, when there is one, is visible, enabled and focusable;
  - every focus edge names an existing control;
  - the chrome is not hidden while a menu is open.
- The machine reads no system clock and uses no randomness.

### Prototype gate

#97 starts with a prototype. The rest of #97 builds on it only after it meets this gate:

- it implements the chrome, menu and focus regions and a minimal ad region, driven by keyboard and D-pad intents;
- it passes golden tests that record intents, engine events and clock advances, and assert snapshots and effects;
- it survives a randomised intent fuzz. Seeded sequences of intents, engine events and clock advances run with the
  invariants checked after every step. CI runs fixed seeds with a bounded number of steps, and a failure reports its
  seed and sequence, which then becomes a golden test;
- it reports the module's code size and clean compile time, and a Google Benchmark of one input followed by one
  snapshot, as baselines.

The implementation escalates to option A, a small in-house statechart runtime with SCXML semantics, only if the
prototype shows that entry and exit bookkeeping is getting messy. The signs are the same exit work repeated across
many transitions, cross-region rules whose correctness depends on their order, or fuzz failures traced to a missed
entry or exit. Escalation is recorded in a new ADR that supersedes this choice.

### Authoring and the diagram

- The machine is written in C++. There is no declarative source and no third generator.
- A test-time tool, built with the UI tests, exports the structure as a Mermaid state diagram with one composite
  state per region. It lists each region's states from its variant alternatives, and finds transitions by applying
  every input kind to sample values of each state that the test code supplies. A state that no input reaches fails
  the tool.
- The diagram is a generated page under `docs/reference/`. It is committed, never edited by hand and checked for
  drift, as the error catalogue is, and the site renders it (ADR-0022). It documents the machine; it is never the
  source of behaviour.

### ViewModel

The ViewModel is designed in full now and populated as features land. It contains:

- **Controls:** id, kind (button, toggle button, slider, menu button or custom), slot, visibility, enabled state,
  pressed or checked state, label key, value text, icon token and whether the control is focusable.
- **Menus:** nested items with an id, a label key, a kind (submenu, single choice or toggle), the selection and the
  highlighted item.
- **Seek bar:** the UI timeline (D-26, #26), the duration including +Infinity, the seekable window, buffered ranges,
  ad and cue markers, the scrub preview position and the playhead.
- **Focus:** the target, the focus graph's edges in each direction and the restore target.
- **Announcements:** a queue of live-region messages, each with a string key, arguments, a politeness level and a
  sequence number.
- **Modes and panels:** ad mode with its input owner, fullscreen, picture-in-picture, the layout direction, the
  buffering spinner, and the error panel with its catalogue code
  ([ADR-0018](0018-error-model-and-recovery.md)).

Rules for the ViewModel:

- It never contains display text. Labels, value text and announcements are string keys with typed arguments that
  renderers format, so golden tests do not depend on a locale.
- Until 1.0, only renderers in this repository consume it. It has no schema version of its own: it is versioned
  with the lockstep release. Renderers outside the repository are not supported before 1.0, and D-34 (#34) decides
  the policy afterwards.
- Each field carries a `stable` or `experimental` label in its sidecar reference documentation, as settings, tokens
  and ADR-0023's API members do. If a `stable` field must change before 1.0, the changelog lists it with the other
  breaking changes.
- Each section (controls, menus, seek bar, focus, announcements, and modes and panels) has a version counter that
  increases whenever the section changes.
- D-18 (#18) projects the ViewModel into the TypeScript, Kotlin and Swift bindings, and copies only the sections
  whose version changed.

### Snapshots and frame pacing

- The machine runs on the embedder's runner, where ADR-0015 delivers engine events and where renderers receive
  input. An intent is reduced as soon as it arrives, with no thread hop, no throttling and no debounce.
- When HTML updates an attribute synchronously, the intent that changes it updates the state in the same step.
  `TogglePlay` flips the play button at once, because `paused` changes immediately, and later engine events confirm
  or correct it, for example when `play()` is rejected.
- Snapshots are immutable and shared. A renderer finds what changed by comparing section version counters, and
  because a snapshot never changes, a renderer may draw from it on its own render thread.
- Publication is coalesced to at most one snapshot per display frame. After a step that changes the ViewModel, the
  machine calls the renderer's frame-request hook once, and the renderer takes the latest snapshot at the start of
  its next frame. An input is therefore visible no later than the next frame, and nothing is published while nothing
  changes. Coalescing never drops announcements or effects, which are queued with sequence numbers.
- The playhead carries a position, a rate and an anchor, which is the time on the player's monotonic clock at which
  the position was sampled. The UI module exposes that clock, so renderers never mix clocks. Each frame, a renderer
  computes `position + rate × (t − anchor)`, where `t` is the frame's target presentation time when the platform
  reports one, such as the timestamp passed to `requestAnimationFrame` or `CADisplayLink.targetTimestamp`, and the
  current time otherwise. The result is clamped to the seekable range. The rate is the media clock's effective rate, the playback rate times
  any resample ratio of [ADR-0024](0024-media-clock-and-av-sync.md), and it is zero while playback is paused,
  buffering or seeking, and while the user scrubs.
- The machine publishes a new playhead only when extrapolation would go wrong: on a rate change, a seek, a
  discontinuity, a state change, or a `timeupdate` that disagrees with the extrapolated position by more than a
  small tolerance. Steady playback produces no snapshot per `timeupdate`. The seek bar moves at the display's
  refresh rate, whether 120 Hz, 144 Hz, higher or variable, and the elapsed-time text changes when its displayed
  second changes.
- While the user scrubs, the thumb follows the pointer, touch or remote input from the scrub state in every frame.
  The engine receives the latest position through `fastSeek()` with at most one seek outstanding, and a precise
  seek on release; #97 tunes this policy.
- Animation belongs to renderers. The machine says what is shown, and renderers animate the change with `motion.*`
  tokens, following the reduced-motion mode. An animation never delays the handling of input.
- One step, an input followed by a snapshot, must stay well within a 240 Hz frame (4.2 ms) on the reference
  desktop. The benchmark from the prototype gate tracks this.

### Input

- Renderers normalise physical input into intents. Pointer, touch, keyboard, D-pad, remote and media-key input each
  have a small mapping that is documented for each platform.
- The keyboard map is data in the UI module, shared by every renderer. Its shortcuts follow common web-player
  conventions: space or `k` plays and pauses, `j` and `l` seek, `f` toggles fullscreen, `m` mutes and `c` toggles
  captions. On desktop layouts the left and right arrows seek and the up and down arrows change the volume.
- Touch input toggles the chrome with a tap and, when the setting enables it, seeks with a double tap.

### Focus

- The machine's focus target is authoritative on every platform. Focus edges follow the visual order of slots in
  the logical layout and are mirrored wherever the layout mirrors for right-to-left text.
- Renderers forward arrow keys, D-pad and remote directions as `Direction(direction)` intents and never let a
  platform focus engine choose the next control. The machine decides what a direction does from the layout and the
  focused control: it navigates an open menu, adjusts a focused slider, moves focus, or seeks and changes the
  volume as the desktop keyboard map says. Renderers then move platform focus to the machine's target:
  `FocusRequester` in Compose, `preferredFocusEnvironments` with a focus update request on tvOS, `@FocusState` in
  SwiftUI, and `focus()` with a roving `tabindex` on the web. The desktop renderer draws its own focus indicator.
- A focus change that the platform makes on its own, such as a click on a focusable control, reaches the machine as
  a `Focus(id)` intent, and the machine accepts it or restores its own target.
- Back follows one order everywhere: it closes the open menu, then hides the chrome, and otherwise passes to the
  app (#193, #211).
- When the chrome reappears, focus returns to the restore target, or to the primary control if there is none.
- **Escape hatch for tvOS.** If tvOS testing in #211 shows the focus engine fighting the mirrored focus, tvOS may
  switch to option B: its focus engine moves focus, and the renderer reports each change back as a `Focus(id)`
  intent. Golden tests then cover tvOS's intents but not its navigation, which #211's XCUITest suite covers instead.
  The maintainer approves the switch in #211, where the failing cases are recorded, and it applies to tvOS only.

### Accessibility

- Controls follow WAI-ARIA Authoring Practices Guide (APG) patterns. Renderers map them to ARIA on the web, to
  Compose semantics, to SwiftUI accessibility modifiers, and, once it exists, to the desktop accessibility tree, which
  [ADR-0030](0030-desktop-graphics-audio-and-ui-renderer.md) defers.
  - Seek and volume are sliders with a minimum, a maximum, a current value and value text.
  - Settings, quality, audio track, subtitles and speed are menu buttons that open menus, with single-choice items
    for exclusive choices.
  - Play and pause is one button whose label changes. Mute, fullscreen and captions are toggle buttons with a fixed
    label and a pressed state. The APG button pattern says a toggle button's label must not change with its state,
    so each control either changes its label or carries a pressed state, never both.
- Time is described in localised value text, such as "1 minute 5 seconds of 42 minutes", built from string keys
  with plural arguments. During playback, the seek slider's value text follows the playhead at a coarse interval
  rather than every frame, so that a focused slider does not make screen readers chatter; #97 sets the interval.
- Live announcements cover only discrete events: play, pause, a seek that lands, an error and the start of an ad
  break. Continuous progress is never announced. Errors are assertive, and the other announcements are polite.
- The M1 minimum is roles, labels, states and value text in the ViewModel, and full keyboard operation of the
  desktop renderer: every control is reachable and operable from the keyboard, with a visible focus indicator
  styled by the `focus` token state. Screen-reader exposure on desktop waits for a platform accessibility tree, which ADR-0030 defers. The web renderer adds
  it in M4 (#154), the Compose renderer in M6 and the SwiftUI renderer in M7, and M11 audits every renderer (#271).

### Strings and locales

- One catalogue in the repository holds every UI string as an ICU MessageFormat message, with plural and select
  arguments, a dotted `lower_snake` key and a description for translators. The task that builds it chooses TOML or
  JSON for the file, favouring the format that translation tools import.
- A generator built on the infrastructure of the settings and token generators (stdlib-only Python, committed
  outputs and a drift check) emits Android string resources, Apple String Catalogs, web JSON and a C++ table for
  the desktop renderer. It fails on any message that one of the targets cannot represent, so the four outputs never
  disagree.
- Renderers format numbers, durations and dates with their platform's locale facilities. They choose the locale,
  from the platform or the app, and pass the machine only the layout direction, which the focus graph needs.
- English ships first. Tests add an accented, lengthened pseudo-locale and a mirrored right-to-left pseudo-locale,
  modelled on Android's `en-XA` and `ar-XB`. The C++ golden tests check the mirrored focus graph, and each
  renderer's screenshot tests run in both pseudo-locales.

### Customisation

- Settings hide and reorder built-in controls (`ui.controls.hidden` and `ui.controls.order`, listed in the
  follow-ups). A hidden control leaves the focus graph and cannot be activated.
- An API on the UI module registers custom controls, each with an id, a label key, an icon token and a slot. A
  custom control appears in the ViewModel as a control of kind `custom`. It joins the focus graph and the
  accessibility model like a built-in control, and when activated it emits a `CustomAction(id)` event to the app
  through ADR-0023's typed subscription. The app can change a custom control's visibility, enabled state and pressed
  state.
- The label key of a custom control resolves in the app's own string resources. Its icon token names an icon in the
  shared icon set or one that the app registers with its renderer.
- Slots are designed for each logical layout (desktop, mobile and TV), so a custom control has a defined place and
  defined focus neighbours in each of them.

### Conflicts with accepted decisions

- The issue's option C, XState in JavaScript, conflicts with ADR-0007's headless state machine in C++, unless it is
  reduced to a code generator. The second question's declarative option covers a generator, and it is not chosen.
- ADR-0007 already assigns the focus graph to `umbra::ui`, so the issue's question "Is the focus graph defined by
  the machine, or delegated to platform focus engines?" is settled. This ADR decides only how the graph coexists
  with those engines.
- ADR-0021 already requires golden ViewModel tests. This ADR defines what they record, not whether they exist.

The issue's proposal is also corrected in three places:

1. It recommends an in-house statechart runtime. This ADR chooses the typed reducer and keeps the runtime as the
   escalation path.
2. It publishes a snapshot after every transition. This ADR coalesces snapshots to at most one per display frame.
3. Its proposed key `ui.seek.step_seconds` breaks ADR-0017's rule that a key ends in its unit, so the step becomes
   `ui.controls.seek_step_ms`, as #97 already proposes.

## Consequences

**Positive**
- Behaviour, focus, accessibility semantics and strings are defined once, and golden tests cover them for all four
  renderers.
- Controls respond in the next frame, and the seek bar moves smoothly at the display's refresh rate with few
  snapshots.
- A region cannot represent an invalid state, and the fuzz checks the cross-region invariants.
- The machine is plain C++ with no dependency and no runtime to own, and it still has a generated diagram.
- Custom controls keep D-pad focus and accessibility.
- Right-to-left and text-expansion bugs surface in tests before any translation exists.

**Negative / costs**
- Entry and exit work and cross-region invariants are written by hand, and the escalation to a runtime may still be
  needed.
- Every renderer implements the frame-request hook and extrapolates the playhead.
- Renderers override platform focus engines, and the tvOS focus engine may push back.
- Renderers outside the repository must wait until after 1.0, and the stability labels need upkeep.
- The desktop renderer has no screen-reader exposure until a later milestone adds a platform accessibility tree.
- The string generator is one more generator, and every message must fit the Android, Apple, web and C++ formats.
- Slots must be designed for each layout.
- Changing behaviour always means writing C++.

**Follow-ups**
- #97 builds the prototype and meets the gate, then the regions, cross-region rules, invariants, effects,
  ViewModel, frame-coalesced publication, playhead, diagram tool and page, golden tests and fuzz. It also decides
  whether the seek bar and the transport controls mirror in right-to-left layouts: Material Design keeps media
  controls left to right, while Apple's current guidance flips controls that show progress. One rule applies to
  every renderer.
- #98 implements the frame-request hook, playhead extrapolation, the focus indicator and the shared keyboard map in
  the desktop renderer, and runs its screenshot tests in both pseudo-locales.
- D-19 (#19, ADR-0030) supplies the desktop renderer's frame callback. A platform accessibility tree for desktop
  needs its own task.
- The web renderer (#154, M4), the Compose renderer and Google TV focus (#192 and #193, M6), and the SwiftUI
  renderer and tvOS focus (#210 and #211, M7) mirror the focus target, extrapolate the playhead and map roles and
  value text on their platforms. #211 decides whether tvOS needs the escape hatch.
- #164 (M5) completes the ad region with D-23 (#23). D-24 (#24) defines the presentation region, and D-26 (#26)
  supplies the seek bar's timeline.
- #96 and D-02 (#2) give the machine playhead samples stamped with the monotonic time at which they were taken, so
  that the anchor reflects presentation rather than the moment an event was drained.
- D-18 (#18) projects the ViewModel and the custom-control API into the bindings.
- A new task builds the string catalogue and its generator, chooses the file format, and decides how the C++ table
  selects plural forms for locales after English without the size of ICU4C, within ADR-0014.
- A new task defines the shared icon set that icon tokens name, since DTCG has no icon type.
- #53 adds these `ui.*` keys to the schema. They are listed here, not added, and #53 fixes their final names:
  - `ui.controls.auto_hide_ms`: inactivity before the chrome hides (#97 proposes 3000);
  - `ui.controls.seek_step_ms`: the keyboard and D-pad seek step (#97 proposes 5000; #193 proposes a separate TV
    step as `ui.tv.seek_step_ms`, which #53 checks against ADR-0017's rule that platforms never appear in keys);
  - `ui.controls.volume_step`: the keyboard volume step (#97 proposes 0.05);
  - `ui.spinner.delay_ms`: the delay before the buffering spinner appears (#97 proposes 500);
  - `ui.controls.hidden`: a string list of built-in controls to hide;
  - `ui.controls.order`: a string list that orders built-in controls within their slots;
  - `ui.double_tap_seek.enabled`: whether a double tap seeks on touch screens.
- #271 (M11) audits every renderer against these accessibility rules.
- D-34 (#34) decides how the ViewModel is versioned for renderers outside the repository after 1.0.

## Pros and cons of the options

### Implementation of the machine

- **A. In-house statechart runtime.** Good, because it is formal, handles entry and exit actions and parallel
  regions for us, and exports diagrams easily. Bad, because it is a runtime to specify, test and document without
  comments.
- **B. Boost.SML.** Good, because there is no runtime to own and transitions are checked at compile time. Bad,
  because compile times are heavy and error messages are hard to read.
- **C. Typed reducer over variants.** Good, because it is plain C++, the quickest to build, invalid states cannot
  be represented within a region, and golden tests are trivial. Bad, because entry and exit work and cross-region
  invariants are written by hand.

### Authoring

- **A. C++ source and a generated diagram.** Good, because it keeps one language, refactoring is safe and there is
  still an overview diagram. Bad, because behaviour cannot be changed without writing code.
- **B. Declarative source and a generator.** Good, because it is self-documenting and has visual tooling. Bad,
  because it adds a third generator and a semantics specification to own.
- **C. C++ only.** Good, because it is the least work. Bad, because there is no overview diagram.

### ViewModel contents and versioning

- **A. Full model for in-repository renderers.** Good, because renderers do not churn as features land, and the
  design stays free to change before 1.0. Bad, because renderers outside the repository must wait.
- **B. Public and separately versioned.** Good, because it suits an ecosystem of renderers. Bad, because it freezes
  a young design.
- **C. M1-minimal model.** Good, because it is quickest. Bad, because adding menus, ads and focus later changes
  every renderer.

### Snapshot delivery

- **A. Coalesced snapshots with an extrapolated playhead.** Good, because scrubbing and progress are smooth at any
  refresh rate, change checks are cheap, and renderers are not flooded. Bad, because each renderer extrapolates the
  playhead.
- **B. A snapshot on every transition.** Good, because it is simplest. Bad, because the playhead steps every 250 ms
  and snapshots churn.
- **C. Diffs or patches.** Good, because the least data moves. Bad, because every renderer must apply patches
  correctly, which is harder to test.

### Focus and platform focus engines

- **A. The machine leads and renderers mirror it.** Good, because navigation is identical everywhere and testable
  in C++. Bad, because the tvOS focus engine may push back in edge cases.
- **B. The machine leads except on tvOS.** Good, because tvOS keeps its native feel, such as parallax and focus
  sounds. Bad, because one platform's navigation is not covered by golden tests.

### Accessibility

- **A. APG patterns with quiet announcements.** Good, because it matches screen-reader expectations at a small M1
  cost and fixes the accessibility model now. Bad, because desktop screen-reader exposure is still missing.
- **B. Full screen-reader support in M1.** Good, because the player is accessible from the first release. Bad,
  because the desktop stack has no platform accessibility tree, so M1's scope would grow a lot.
- **C. Defer to M4.** Good, because there is nothing to do in M1. Bad, because roles have to be retrofitted into the
  ViewModel and the desktop renderer later.

### String catalogue

- **A. One catalogue with generated outputs.** Good, because there is one translation workflow, strings stay
  consistent across the four renderers, and right-to-left bugs are caught early. Bad, because it adds one more
  generator output.
- **B. Native catalogues per platform.** Good, because each platform keeps its own tooling. Bad, because strings
  drift between platforms.
- **C. gettext `.po` files.** Good, because the tools are established. Bad, because they fit the Apple and Android
  formats poorly.

### Customisation

- **A. Hide, reorder and declare controls.** Good, because custom controls keep D-pad focus and accessibility. Bad,
  because slots must be designed for each layout.
- **B. Renderer slots only.** Good, because apps have complete freedom. Bad, because the focus graph and
  announcements break around views the machine does not know about.
- **C. Replace or fork the machine.** Good, because there is no API to design. Bad, because forks drift.

## References

- [D-28 discussion](https://github.com/LuisAhumadaMartens/alumbra/issues/28)
- [WAI-ARIA Authoring Practices Guide](https://www.w3.org/WAI/ARIA/apg/):
  [slider](https://www.w3.org/WAI/ARIA/apg/patterns/slider/) ·
  [menu button](https://www.w3.org/WAI/ARIA/apg/patterns/menu-button/) ·
  [menu and menubar](https://www.w3.org/WAI/ARIA/apg/patterns/menubar/) ·
  [button](https://www.w3.org/WAI/ARIA/apg/patterns/button/)
- [WAI-ARIA 1.2: `aria-live`](https://www.w3.org/TR/wai-aria-1.2/#aria-live)
- [W3C WAI: Accessible media players](https://www.w3.org/WAI/media/av/player/)
- [ICU: Formatting messages (MessageFormat)](https://unicode-org.github.io/icu/userguide/format_parse/messages/)
- [Unicode CLDR: Plural rules](https://cldr.unicode.org/index/cldr-spec/plural-rules)
- [Apple Xcode: String Catalogs](https://developer.apple.com/documentation/xcode/localizing-and-varying-text-with-a-string-catalog)
- [Android: String resources](https://developer.android.com/guide/topics/resources/string-resource) ·
  [pseudolocales](https://developer.android.com/guide/topics/resources/pseudolocales)
- [Jetpack Compose: Focus](https://developer.android.com/develop/ui/compose/touch-input/focus) ·
  [change focus behaviour](https://developer.android.com/develop/ui/compose/touch-input/focus/change-focus-behavior) ·
  [`FocusRequester`](https://developer.android.com/reference/kotlin/androidx/compose/ui/focus/FocusRequester)
- [Apple: About focus interactions for Apple TV](https://developer.apple.com/documentation/uikit/about-focus-interactions-for-apple-tv) ·
  [`preferredFocusEnvironments`](https://developer.apple.com/documentation/uikit/uifocusenvironment/preferredfocusenvironments) ·
  [SwiftUI `FocusState`](https://developer.apple.com/documentation/swiftui/focusstate)
- [HTML: Animation frames](https://html.spec.whatwg.org/multipage/imagebitmap-and-animations.html#animation-frames) ·
  [Apple: `CADisplayLink`](https://developer.apple.com/documentation/quartzcore/cadisplaylink)
- [Material Design: Bidirectionality](https://m2.material.io/design/usability/bidirectionality.html) ·
  [Apple Human Interface Guidelines: Right to left](https://developer.apple.com/design/human-interface-guidelines/right-to-left)
- [W3C: SCXML](https://www.w3.org/TR/scxml/) · [Boost.SML](https://github.com/boost-ext/sml)
- [Mermaid: State diagrams](https://mermaid.js.org/syntax/stateDiagram.html)
- [ADR-0007](0007-ui-architecture.md), [ADR-0012](0012-repository-topology-and-packaging.md),
  [ADR-0015](0015-threading-and-task-runners.md), [ADR-0017](0017-settings-schema-and-configuration.md),
  [ADR-0018](0018-error-model-and-recovery.md), [ADR-0019](0019-design-tokens.md),
  [ADR-0021](0021-testing-strategy.md), [ADR-0022](0022-documentation-site-and-api-reference.md),
  [ADR-0023](0023-public-api-surface-and-media-error.md)
