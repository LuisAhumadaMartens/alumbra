# API conformance suite

Proves that `umbra::player::Player` behaves like `HTMLMediaElement` for the v1 subset
([ADR-0023](../../docs/adr/0023-public-api-surface-and-media-error.md), issue #100). It has two halves, both
deterministic ([ADR-0021](../../docs/adr/0021-testing-strategy.md)): every test runs on the fake port with
the fake clock, so no test sleeps or reads a real clock.

## Layout

| Path | Purpose |
|------|---------|
| `wpt_media_elements_test.cc` | Web-platform-tests media cases ported to GoogleTest. Each suite is named after the WPT file it ports (`WptSeekingSeekToCurrentTime` ports `seeking/seek-to-currentTime.html`). |
| `wpt-index.md` | The mapping file: every WPT case as adopted, adapted, deviation, not-applicable or deferred, with its reason and its Alumbra tests. |
| `traces/*.trace` | Expected event sequences as data, replayed by `event_trace_test.cc`. |
| `event_trace.h`, `event_trace.cc` | The trace parser and the sequence comparator. |
| `element_harness.h`, `element_harness.cc` | `ElementHarness`: the fake-port fixture plus an event log and the trace step interpreter. |
| `event_trace_test.cc` | Replays every trace, and tests the parser and the comparator. |

`umbra_api_conformance` (alias `alumbra_test::api_conformance`) is the reusable library: the trace format, the comparator
and the harness. The SDK suites (#153, #191, #209) read the same `traces/` files and the same `wpt-index.md`, and drive
their own player through the step vocabulary below; a binding does not link the C++ harness.

## Running it

```sh
cmake --workflow --preset debug
ctest --preset debug -L api_conformance
```

The label `api_conformance` selects the GoogleTest binary and the two index checks; `fake_port` marks the tests that run
on the fake port. #65 is expected to define the shared label set, which will replace these two plain ctest labels.

The table in [HTMLMediaElement conformance](../../docs/reference/html-media-element-conformance.md) is generated from
`umbra/player/conformance.toml`. Its rows name these tests, so the coverage figures at the top of that page come from
this suite.

## Porting rules

- The WPT path is cited by the suite name and by `wpt-index.md`, never by a comment.
- A browser test that waits for an event becomes a test that calls `DispatchEvents` (`Pull()` in the harness) and reads
  the state carried by the event, because events are queued and carry the values of the moment they fired.
- A setter that throws in the browser (`TypeError`, `IndexSizeError`, `NotSupportedError`) returns `false` here, and the
  test asserts that the value is unchanged.
- A case that fails on Alumbra today is ported with its original assertions under a `DISABLED_` name, listed as a
  `deviation` row, and linked from `conformance.toml` with an issue. Enabling the test is part of fixing the issue.
- A case that needs a DOM, source children, text tracks, a manual check or a browser-only API is `not-applicable`
  with the reason written out. A case that needs decoded media on a real port is `deferred`.

## Known deviations

| Issue | WPT case | Behaviour |
|-------|----------|-----------|
| [#408](https://github.com/LuisAhumadaMartens/alumbra/issues/408) | `error-sequence.html` | `loadstart` is queued synchronously instead of at a stable state. |
| [#409](https://github.com/LuisAhumadaMartens/alumbra/issues/409) | `load-removes-queued-error-event.html` | `load()` from a listener cannot remove events already drained for delivery. |
| [#410](https://github.com/LuisAhumadaMartens/alumbra/issues/410) | `offsets-into-the-media-resource/currentTime.html` | `currentTime` before metadata is clamped to the `MediaTime` range. |
| [#411](https://github.com/LuisAhumadaMartens/alumbra/issues/411) | `loop-from-ended.tentative.html` | `ended` stays true after `loop` is set. |
| [#412](https://github.com/LuisAhumadaMartens/alumbra/issues/412) | `resource-selection-invoke-{load,play,pause}.html` | No transient `NETWORK_NO_SOURCE`. |
| [#414](https://github.com/LuisAhumadaMartens/alumbra/issues/414) | `mime-types/canPlayType.html` | A dangling or empty parameter answers the empty string instead of `maybe`. |

## Event traces

A trace is a text file of `key: value` lines:

```
scenario: seek-while-playing
provenance: html-spec 4.8.12.9 seeking algorithm; not recorded from a browser
ignore: progress suspend
step: seek 1
expect: seeking timeupdate waiting
```

- `scenario` equals the file name without `.trace`.
- `provenance` starts with `html-spec` or `chrome-recorded`, followed by where the sequence comes from.
- `ignore` lists event names removed from the observed sequence before comparing: timer-driven events whose count the
  HTML Standard leaves to the user agent.
- Each `step` names an action and is followed by one `expect` line, which may be empty. Events are pulled with
  `DispatchEvents` after every step, so one `expect` line holds the events that step produced, in order. Play results
  appear as `result:ok` and `result:rejected` at their place in the sequence.
- Steps: `set_src <duration_ms>`, `set_src_empty`, `open`, `fail_open`, `preroll`, `presenting`, `play`, `pause`,
  `seek <seconds>`, `complete_seek`, `finish_playback`, `load`, `set_rate <x>`, `set_volume <x>`, `set_muted <0|1>`,
  `set_loop <0|1>`, `set_autoplay <0|1>`, `advance <ms>`.

### Provenance

No trace in this directory was recorded from Chrome. Recording needs a desktop browser driven to play a known file, and
none was available when the suite was written. Every sequence is written down from the HTML Standard algorithms that
the `provenance` line cites, and cross-checked against the WPT cases that assert the same order (the `error-sequence`,
`load-events-networkState` and `seek-to-currentTime` files). Treat them as the specification's expectation, not as
observed browser behaviour.

To replace one with a recording: play the same scenario in Chrome with a listener on every event type in
`umbra/player/include/umbra/player/events.h`, write the observed names into the `expect` lines, change the `provenance`
to `chrome-recorded` with the Chrome version and date, and keep the file only if the replay still passes or the
difference is explained in `wpt-index.md`. ADR-0023 makes browsers the oracle, so where Chrome and the specification
disagree the recording wins.

The comparator checks event order only. ADR-0023 also asks for the attribute values seen at each event; the harness
already keeps them (`ObservedEvent::state`), and adding them to the trace format is a follow-up on #100.

## Desktop port

The issue asks for a headless desktop run as well. The public `Player::Create` builds a player on an unavailable port
factory today, because the desktop port has no player factory yet, so a load fails with `MEDIA_ERR_SRC_NOT_SUPPORTED`.
The `WptPublicPlayer` tests run the cases that need no media (initial values, the empty-`src` error, rejected setter
values) against that public player. The full run on the desktop port, including the three `deferred` rows, follows when
the desktop port can play media.
