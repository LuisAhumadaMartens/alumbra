# Testing guide

This is the daily guide to testing Alumbra: which kind of test to write, where it lives, how CTest labels it and how
flaky tests are handled. [ADR-0021](adr/0021-testing-strategy.md) is the source of truth for the strategy; this page
turns it into practice. The inventory of what exists is in [test/README.md](../test/README.md).

## Choosing a kind of test

| Kind | Write it when | Where it lives | Reference example |
|------|---------------|----------------|-------------------|
| Unit | You test one class or function with fakes for time, network and ports | `test/<module path>/<name>_test.cc`, mirroring the source path | [`test/umbra/base/status_test.cc`](../test/umbra/base/status_test.cc) |
| Integration | Several real components run together, or a build, install or tooling check runs a whole flow | Beside the components it joins, or `test/build/` for build-system checks | [`test/penumbra_reference_player/`](../test/penumbra_reference_player), `alumbra_installed_consumer` |
| Conformance | A behaviour must hold for every port, or for every stream in the manifest | `test/penumbra_conformance/`, `test/conformance/` | [`test/penumbra_interface/fake_port_conformance_test.cc`](../test/penumbra_interface/fake_port_conformance_test.cc), `conformance.asset.*` |
| Golden | Output frames or text are compared with a reference | `test/penumbra_desktop/` for video; references live in the test or in `test/golden/` | [`test/penumbra_desktop/video_golden_test.cc`](../test/penumbra_desktop/video_golden_test.cc) |
| Fuzz | A parser reads untrusted input | `test/fuzz/<target>/` | [`test/fuzz/byte_reader_fuzzer/`](../test/fuzz) |
| Benchmark | You assert a measured cost, such as the disabled-tracing overhead | Beside the unit tests, as `*_benchmark.cc` | [`test/umbra/base/task_runner_benchmark.cc`](../test/umbra/base/task_runner_benchmark.cc) |
| Sanitizer canary | A sanitizer must be shown to fail on a known defect | `test/sanitizers/` | `sanitizer_canary_*`, built with `ALUMBRA_SANITIZER_CANARIES=ON` |

Start with a unit test. Add the other kinds when the change touches what they cover: conformance and golden tests
for media behaviour, a fuzz target for every parser of untrusted input.

## Unit tests

Unit tests use GoogleTest. Name the file `<name>_test.cc` and put it next to its siblings under `test/`, in the same
relative directory as the source it covers. They must be fast, hermetic and deterministic: no network, no real
clock, no files outside the build tree.

```cmake
add_executable(umbra_example_unittests widget_test.cc)
target_link_libraries(umbra_example_unittests PRIVATE umbra::example GTest::gtest_main)
alumbra_target_options(umbra_example_unittests)
alumbra_add_test(TARGET umbra_example_unittests LABELS unit)
```

## Integration tests

Integration tests join real components, usually over the fake port in `test/penumbra_interface/`, or check the build
itself, such as the installed consumer and the layering checks. They may be slower than unit tests but remain
offline and deterministic.

## Conformance tests

Two systems provide conformance tests.

- **Manifest-driven streams.** [`test/conformance/manifest.yaml`](../test/conformance/manifest.yaml)
  lists every test stream with provenance, licence evidence, checksums and expected behaviour. Each entry becomes a
  `conformance.asset.<id>` test labelled `conformance` and with its manifest tags. Remote streams carry the
  `network` label and never gate a merge. See the [asset documentation](../test/conformance/README.md).
- **Port conformance kit.** The kit of parameterised suites in `test/penumbra_conformance/` is what every Penumbra
  port instantiates with its own factory, as described in the [porting guide](architecture/porting-guide.md). The
  fake port runs it in `penumbra_fake_port_conformance_test`, and deliberately broken fakes prove that the suites
  fail when they should.

Fake ports are test tools, not evidence that a real port conforms. A claimed platform needs the kit run against that
port.

## Golden frames

A golden test renders output and compares it with a reference. ADR-0021 allows exact hashes only for a decoder, build
and configuration shown to be deterministic. For GPU or scaled output, compare against an independent reference with
a calibrated per-case tolerance and record why that tolerance is enough. No project-wide image threshold exists:
every tolerance is reviewed with the case that uses it, and a golden test needs a demonstration that an intended
regression fails it. [Desktop video output](architecture/desktop-video.md#tests) records the calibrated values for
Metal and lavapipe. Document how to update a reference next to the test that owns it. Label these tests `golden`
and keep large reference media out of the repository; reference it by URL or generate it with a committed script.

## Fuzzing

Every parser of untrusted input has a libFuzzer target registered with `alumbra_add_fuzz_target`. The helper builds
the target as a corpus-replay test on every preset and as a libFuzzer executable under the `fuzz` preset. Seed corpora,
dictionaries and minimised regression inputs live with the target. See the [fuzz target guide](../test/fuzz/README.md).

## Sanitizers

CI runs ASan with UBSan and, separately, TSan over the full suite. Build
locally with `cmake --workflow --preset asan` or `--preset tsan`. Every suppression needs a linked issue. The
`sanitizer_canary` tests prove that each sanitizer still reports a known defect; they exist only when
`ALUMBRA_SANITIZER_CANARIES=ON`.

## Benchmarks

Benchmarks use Google Benchmark and register as tests labelled `benchmark`, so a regression in a measured overhead
fails the suite. Do not invent thresholds: a budget needs a measured baseline ([ADR-0021](adr/0021-testing-strategy.md)).

## Deterministic time

Components read time through an injected `TickClock`, never the system clock. Tests use `FakeTickClock` and
`TestTaskEnvironment` from `umbra/base/test_support/` and advance time with `FastForwardBy` or `RunUntilIdle`. A test
that uses the fake clock must not sleep in real time: the `fake_clock_sleep_lint` test fails the build when it finds a
`sleep` call there. Waiting on a real thread uses `WaitableEvent`, not a delay. See [threading](architecture/threading.md).

## Labels

CTest labels select tests; `ctest --print-labels` lists all of them. Every test carries **exactly one kind label**,
and any number of modifiers.

| Kind label | Meaning |
|------------|---------|
| `unit` | Fast, hermetic, deterministic test of one unit. The default for GoogleTest binaries |
| `integration` | Real components together, or build, install and tooling checks. The default for other tests |
| `conformance` | Port conformance kit, manifest-driven streams and the broken-fake checks |
| `golden` | Reference-output comparisons |
| `benchmark` | Google Benchmark overhead checks |
| `fuzz` | Corpus replays, smoke runs and canaries of fuzz targets |
| `sanitizer_canary` | Proofs that a sanitizer reports a known defect |

| Modifier | Meaning |
|----------|---------|
| `network` | Needs an external network. Never gates a merge. Remote conformance streams get it automatically |
| `slow` | Takes long enough that developers may skip it locally. Use it with `integration`, `golden` or `conformance` |
| `quarantine` | Known flaky; see below |

| Category label | Meaning |
|----------------|---------|
| `api_conformance` | Selects the public player API conformance suite and its index checks |
| `fake_port` | Marks tests that exercise the fake Penumbra port |

Existing category labels stay as they were and are added to a kind: `layering`, `cxx_probe`, `conformance_tools`,
`design_tokens`, `fuzz_replay`, `fuzz_smoke`, `fuzz_canary`, `api_conformance` and `fake_port`. Conformance tests also
carry their manifest tags.

Select with anchored expressions, because CTest matches label regular expressions anywhere in the label:

```sh
ctest --preset debug -L '^unit$'
ctest --preset debug -L '^golden$'
ctest --preset debug -L '^integration$' -LE '^slow$'
ctest --preset debug -L '^conformance$'
```

CI verifies on every C++ change that each test has exactly one kind label, that `-L '^unit$'` selects only unit
tests and that every quarantined test names an issue (`tools/testing/check_test_labels.py`).

### How tests get their labels

Existing registrations need no label arguments. `cmake/AlumbraTesting.cmake` applies defaults centrally:

- A `gtest_discover_tests` call gets `unit`, unless the target is listed in the target table in that file (for example
  `penumbra_fake_port_conformance_test` is `conformance`). An explicit `LABELS` property, such as `golden`, is kept and
  completed with a kind if it lacks one.
- An `add_test` call gets the kind implied by an existing label (`layering` is `integration`, `design_tokens` is
  `golden`, `fuzz_replay` is `fuzz`), then by the name rules in that file (`*_generator`, `*.unit` and `*_unittests`
  are `unit`; the port-conformance checks are `conformance`), and otherwise `integration`.

**New tests use `alumbra_add_test` instead and name their kind.** It is the one place that checks labels:

```cmake
alumbra_add_test(TARGET umbra_example_unittests LABELS unit)
alumbra_add_test(NAME example_tool_check
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/example/check.py"
  LABELS integration slow)
```

`TARGET` discovers every GoogleTest case of an executable with `TEST_PREFIX` defaulting to `<target>.`; `NAME` with
`COMMAND` registers one test. `PROPERTIES` passes extra test properties and `WORKING_DIRECTORY` is accepted for
`TARGET`. Configuration fails when `LABELS` holds no kind, two kinds, or a label that is not registered in
`cmake/AlumbraTesting.cmake`. Register a new label there and in the tables above in the same change.

## Flaky tests

A flaky test is a defect, not noise. ADR-0021 allows no tolerated flake rate for blocking checks: preserve the first
failure and its diagnostics, and do not retry a gate until it passes.

If a test cannot be fixed at once, quarantine it:

1. Open an issue that names the owner, the reason, an expiry and what coverage is lost.
2. Register the test with `QUARANTINE <issue-number>`; this adds the `quarantine` label:

   ```cmake
   alumbra_add_test(TARGET umbra_flaky_unittests LABELS unit QUARANTINE 123)
   ```

   Quarantine covers a whole registration, so move the flaky cases into their own executable first.
3. Quarantined tests are excluded from the test presets and so no longer gate merges. CI runs them in a separate,
   non-blocking step and lists them with their issues in the job summary, so quarantine stays visible.
4. Remove `QUARANTINE` when the fix lands. Quarantine is not verified coverage: a quarantined case never counts toward
   a release claim, and quarantining a required case leaves its evidence gap open.

A quarantined test without an issue fails configuration.

## Where to go next

- [ADR-0021: testing strategy](adr/0021-testing-strategy.md)
- [Test assets and suites](../test/README.md)
- [Porting guide and conformance kit](architecture/porting-guide.md)
- [GoogleTest primer](https://google.github.io/googletest/primer.html)
- [Test sizes](https://testing.googleblog.com/2010/12/test-sizes.html)
