# Porting guide

A Penumbra port implements the [interface v1 contract](port-contract.md) for one platform. This page explains how a
port proves it with the **port conformance kit** (#71), the executable definition of "done" for a port under
[ADR-0021](../adr/0021-testing-strategy.md). The kit is the Alumbra counterpart of Cobalt's NPLB: one set of
parameterised GoogleTest suites that every port, desktop now and web, Android and Apple later, instantiates with
its own factory.

## Kit placement

The kit lives in `test/penumbra_conformance/` and is **test-only**. It is not installed and is not part of the
exported CMake package, for these reasons:

- It needs GoogleTest, Abseil and the non-installed `internal_include/` Penumbra headers. Installed public headers
  stay exactly C++20 with no Abseil ([ADR-0013](../adr/0013-cpp-language-standard.md), [ADR-0018](../adr/0018-error-model-and-recovery.md));
  installing the kit would break that boundary.
- Ports are in-tree and rebuild together with the core under one toolchain (see
  [Placement and versioning](port-contract.md#placement-and-versioning)), so every port's test binary can link the
  kit from the source tree.
- It follows the C++20-only feature policy in [C++ feature policy](../cpp-features.md).

| CMake target | Contents |
|--------------|----------|
| `penumbra::conformance_support` | Static library with `ControlRunnerAdapter`, which wraps Umbra's `SequencedTaskRunner` as the contract's `ControlTaskRunner`. Safe to link into any test. |
| `penumbra::conformance_kit` | Object library with the nine parameterised suites, the fixture and the report listener. |
| `penumbra::conformance_main` | Object library with `main`, which installs the report listener. |

The kit and main targets are object libraries on purpose: a static library would let the linker drop suites that
nothing references, and an unreferenced suite would silently not run.

## Running the kit against a port

A port provides one `PortHarness` and one `PortCase`, then instantiates the suites.

1. Subclass `penumbra::conformance::PortHarness`. It owns the port under test and returns it from `port()`. The
   constructor-side factory receives the `ControlTaskRunner` that the kit drives with Umbra's virtual clock
   (`TestTaskEnvironment`); the port must run its control sequence on it. All other virtual methods are optional
   hooks that default to `kUnimplemented`:

   | Hook | What it must do | Capability |
   |------|-----------------|------------|
   | `RequestData` | Make the player emit `NeedsData{track, max_samples}` for the current ticket, and return the `OnEvent` delivery status. A port that issues grants itself leaves it unimplemented. | `kDataRequestHook` |
   | `RequestDecoderData` | The same for a granular `Decoder`. | `kDataRequestHook` |
   | `InjectError` | Make the player report the given `PortError` through its callbacks. | `kErrorInjection` |
   | `AdvancePlayback` | Let the given amount of playback time pass for the player (a test-only clock step). | `kClockAdvance` |
   | `DriveVsync` | Make a started `VideoSink` call `TakeDueFrame` once, as its vsync thread would, and return the frame. | `kVsyncDrive` |
   | `DriveAudioRender` | Make a started `AudioSink` fill one buffer from its source, as its audio thread would. | `kAudioRenderDrive` |

2. Describe the port with a `PortCase`: a `name` (letters, digits and underscores; it becomes the GoogleTest
   parameter name and the port name in the report), the `factory`, and the set of `Capability` values the port
   claims.
3. Write a test binary that links `penumbra::conformance_kit`, `penumbra::conformance_main` and the port, and
   instantiate every suite with one macro:

   ```cpp
   #include "penumbra_conformance/conformance.h"

   namespace {
   const std::vector<penumbra::conformance::PortCase>& Cases();
   }

   ALUMBRA_INSTANTIATE_PORT_CONFORMANCE(Conformance, Cases());
   ```

   The macro instantiates all nine suites. GoogleTest fails a binary that defines a parameterised suite and never
   instantiates it, so a port cannot skip a suite by leaving it out.
4. Register the binary with `gtest_discover_tests` and run it in CI on every platform leg where the port builds
   ([ADR-0021](../adr/0021-testing-strategy.md) requires target-executable kit runs against fake and actual ports).
   `test/penumbra_interface/` is the worked example: `fake_harness.*` is the harness, and
   `fake_port_conformance_test.cc` the instantiation.

Tests run on a deterministic in-process control sequence with a fake clock: no sleeps, no network and no real time.
The synthetic streams match the shape of the `frame-numbered-h264-mp4` and `tone-1khz-2khz-aac-m4a` clips of the
[conformance manifest](../../test/conformance/README.md) (640x360 H.264 and 48 kHz stereo AAC); the payload bytes
are synthetic because the kit exercises the port contract, not codecs.

## Capability gates and skips

A test that needs a hook or an optional path begins with `ALUMBRA_PORT_REQUIRE(capability)`. When the port does not
claim the capability, the test reports **skipped** (GoogleTest `GTEST_SKIP`, shown as `[  SKIPPED ]` and mapped to
a skipped CTest result), never passed. Claim a capability only when the hook works: an overclaim fails tests, an
underclaim shows up as skips in the report. Tests that make no assumption about hooks run for every port.

Some tests also skip on a property of the port: the instance-limit test skips when `max_players` is too large to
exhaust, and the supported-streams test skips when the port does not decode the synthetic clip codecs.

## Suites

| Suite | Covers |
|-------|--------|
| `LifecycleConformance` | Limits and API version, create and destroy, terminal idempotent `Stop`, destruction with pending work, creation failure at the instance limit without retained callbacks, invalid configuration, unsupported codec. |
| `FlowControlConformance` | Grants name a known track with a positive maximum, one write per grant, invalid writes keep the grant, per-track and aggregate end of stream, fragment input. |
| `SeekTicketConformance` | Strictly increasing tickets for Seek and Flush, a failed Seek leaving the generation unchanged, clock reset on Seek and preserved on Flush, counters reset, stale and future tickets, grants and end of stream reset, late events filtered. |
| `ClockConformance` | Media time never decreases while playing, a paused player holds its position, Seek and Flush after playback. |
| `CallbackThreadingConformance` | Delivery inside a `CallbackScope`, no events after `Stop`, control-sequence delivery through `CallbackDispatcher`, observable delivery failure. |
| `ErrorReportingConformance` | `PortError` keeps status, category and recovery hint; Status payloads survive; OK is never reported as an error; errors carry the current ticket. |
| `CapabilityConformance` | Unsupported is `false` and not an error, repeated queries agree, queries allocate no player, `CanChangeType` is stable, false across stream types and consistent with `config_change` writes. |
| `OutputConformance` | Bounds only on punch-out video, current frame only on texture video, playback rate and volume validation, initial counters. |
| `ComponentConformance` | Video and audio sink lifecycle, vsync pulls only due frames and stops at the barrier, short audio reads fill with silence, output latency, decoder grants, flush tickets and output lifetime after destruction. |

## Machine-readable report

Run the test binary with `--conformance_report=<path>` to write JSON for the
future conformance dashboard (#268):

```json
{
  "schema": 1,
  "interface_version": 1,
  "ports": [
    {
      "name": "fake",
      "interface_version": 1,
      "tests": [
        {"suite": "LifecycleConformance", "test": "StopIsTerminalAndIdempotent", "result": "passed"}
      ]
    }
  ]
}
```

`result` is `passed`, `failed` or `skipped`. `interface_version` is `penumbra::kApiVersion` at build time. The
`penumbra_conformance_report` CTest runs the fake port and validates this shape.

## Proving the kit detects broken ports

`penumbra_broken_port_conformance_test` instantiates the kit over deliberately broken fake ports, and CTest registers
each named expectation as `WILL_FAIL`, so the kit regresses loudly if it stops detecting one:

| Broken port | Named test that must fail |
|-------------|---------------------------|
| `broken_reentrant_callback` (delivers events outside a `CallbackScope`, so receivers cannot detect re-entry) | `CallbackThreadingConformance.EventsAreDeliveredInsideACallbackScope` |
| `broken_ignored_seek_ticket` (accepts Seek and Flush without advancing its generation) | `SeekTicketConformance.SeekRequiresAStrictlyIncreasingTicket`, `SeekTicketConformance.StaleWriteIsDiscardedWithoutConsumingTheGrant` |
| `broken_non_monotonic_audio_position` (media clock steps backwards) | `ClockConformance.MediaTimeNeverDecreasesWhilePlaying` |

A filter that matches no test makes GoogleTest exit successfully, which fails a `WILL_FAIL` test, so renaming a test
without updating the table is caught.

## Not covered yet

These cases wait for work that is not on `main`; each is tracked on #71:

- Transport conformance (cancel and stop barriers, pause and resume, monotonic arrival times and declared timing
  precision, ADR-0026) needs the transport interface of #73.
- The fallback-demuxer slot answering `kUnimplemented` and a real result, and the latency and refresh-rate
  reporting of ADR-0024 and ADR-0025, need #322 and the matching interface additions. Monotonic media time is
  covered by `ClockConformance` for the existing snapshot.
- Capability-query speed needs a clock policy that is deterministic for real ports; today only consistency is
  checked.
- Cases driven by decoded manifest streams need a demuxer; the kit uses synthetic samples until one exists.
- The desktop port itself is not implemented yet, so only the fake port runs the kit; the desktop port instantiates
  it the same way when it lands.
