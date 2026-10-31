# Test assets and suites

- `conformance/` — manifest of public test streams (HLS, DASH, LL, ads, DRM) with expected behaviour.
- `golden/` — reference frames for rendering and text-track tests.
- `fuzz/` — seed corpora for parser fuzz targets.

Large media is referenced by URL, never committed. Strategy is open definition **D-31**.
