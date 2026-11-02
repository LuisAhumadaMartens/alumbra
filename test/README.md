# Test assets and suites

The M0 skeleton registers `umbra_base_smoke_test` (a plain CTest executable),
`alumbra_installed_consumer` (install, configure, build and run a separate
consumer of the exported CMake package) and the `layering` tests with their
fixtures in `build/layering/` ([layering checks](../docs/architecture/layering.md)).
All run through the desktop workflow presets; see
[CONTRIBUTING.md](../CONTRIBUTING.md#building-the-desktop-skeleton).

- `umbra/base/` — unit tests for the threading primitives, status helpers and byte/bit readers (`umbra_base_unittests`) and `task_runner_benchmark`
  ([threading](../docs/architecture/threading.md)).
- `sanitizers/` — opt-in ASan/UBSan and TSan canary executables. The CI runner
  expects each canary process to fail with its specific sanitizer diagnostic
  and uploads that output as evidence; normal builds leave them disabled. The
  suppression files are empty; any future suppression entry must cite an issue.
- `build/fake_clock_sleeps/` — fixture proving that `fake_clock_sleep_lint` rejects a real sleep in a fake-clock
  test. `tools/testing/check_fake_clock_sleeps_test.py` covers POSIX and multiline calls, transitive fixture
  headers and include cycles.
- `build/public_header_abseil*` — installed consumer rejection fixtures for `.h`, `.hpp`, `.hh` and `.hxx`.
- `conformance/` — manifest of public test streams (HLS, DASH, LL, ads, DRM) with expected behaviour.
- `golden/` — reference frames for rendering and text-track tests.
- `fuzz/` — libFuzzer targets for every parser of untrusted input, their seed corpora, regression inputs
  and dictionaries, registered with `alumbra_add_fuzz_target`. Each corpus replays in `ctest` on every
  preset (`fuzz_replay.*`); the `fuzz` preset adds the libFuzzer smoke runs and opt-in canaries
  ([fuzz targets](fuzz/README.md)).

Large media is referenced by URL, never committed. Strategy is open definition **D-31**.
