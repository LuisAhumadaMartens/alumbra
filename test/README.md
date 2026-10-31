# Test assets and suites

The M0 skeleton registers `umbra_base_smoke_test` (a plain CTest executable),
`alumbra_installed_consumer` (install, configure, build and run a separate
consumer of the exported CMake package) and the `layering` tests with their
fixtures in `build/layering/` ([layering checks](../docs/architecture/layering.md)).
All run through the desktop workflow presets; see
[CONTRIBUTING.md](../CONTRIBUTING.md#building-the-desktop-skeleton).
GoogleTest and dependency setup arrive in #38.

- `conformance/` — manifest of public test streams (HLS, DASH, LL, ads, DRM) with expected behaviour.
- `golden/` — reference frames for rendering and text-track tests.
- `fuzz/` — seed corpora for parser fuzz targets.

Large media is referenced by URL, never committed. Strategy is open definition **D-31**.
