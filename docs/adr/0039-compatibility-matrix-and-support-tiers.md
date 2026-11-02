# 0039 — Support platforms in evidence-based tiers, with Apple 17/14 and Android 9 floors

- **Status:** Accepted
- **Date:** 2026-11-01
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #22

## Context

D-22 sets the floors for browsers, operating systems, devices and toolchains, and what counts as "verified". Floors
fix binary size, API choices and test cost. `docs/compatibility.md` promises a matrix, and several decisions wait
on it: D-18 needs a Swift and Xcode minimum, D-20 needs the Apple split between sample-buffer APIs, D-21 needs
`minSdk`, and D-16 and D-17 need browser features. D-22 blocks #156, #181, #183, #198, #216 and #268.

Accepted decisions already set part of it:

- [ADR-0013](0013-cpp-language-standard.md) fixes toolchain floors (GCC 15, Clang 21, Xcode 26.6, NDK r30,
  Emscripten 6) and gates `std::format` and `std::print` on the Apple deployment target.
- [ADR-0021](0021-testing-strategy.md) allows no claim without evidence.
- [ADR-0030](0030-desktop-graphics-audio-and-ui-renderer.md) needs presentation-time on Wayland and X11 Present, and
  runs CI without a GPU on lavapipe.
- [ADR-0031](0031-windows-support-scope-and-toolchain.md) makes Windows 11 "engine builds, not supported" until
  after M7.
- The web target is every device with a browser, including TVs.

## Decision drivers

- No support claim without evidence, as in ADR-0021.
- The lowest floors that keep one decode path per platform, so that code and tests do not double.
- Weak devices matter and old devices do not.
- Floors that match across web and native, so that behaviour is explainable.
- A matrix that can change without a new ADR for every cell.

## Considered options

Tiers:

1. **A — A single "supported" list.**
2. **B — Feature floors in tiers**, with evidence per cell.

Apple floors:

1. **iOS, iPadOS and tvOS 17, macOS 14.**
2. **iOS 16.3, macOS 13.3**, with a display-layer fallback for D-20.
3. **iOS 15, macOS 13**, with two renderer paths and `absl::StrFormat` for floats.

Android `minSdk`:

1. **28** (Android 9).
2. **24**, matching the IMA Android floor.
3. **30** (Android 11).

## Decision

Chosen options: **B for tiers, Apple 17/14, and Android `minSdk` 28, with a pre-approved move to 30.**

### Tiers and evidence

- **Tier 1:** the full feature set, verified in CI on every change, or on a named device on a fixed cadence for
  hardware CI cannot run.
- **Tier 2:** verified on a reference device before each release, not on every change.
- **Tier 3:** best effort. It builds or loads, but no promise, and a defect there does not block a release.
- The tier definitions above are this ADR's interpretation of the brief's "option B, feature floors". The matrix may
  refine their wording without a new ADR.
- The matrix lives in a data file with an evidence link per cell (a CI run or a device record) and a generated
  table. Promoting a cell is a reviewed pull request. An ADR is needed only to add a platform or to move a floor.
- Floors move only in a minor release, with a release-note notice.

### Web

- **Desktop:** Chromium browsers and Firefox, current and previous version, plus Firefox ESR. Safari 17.1 or newer.
  Tier 1, through Playwright.
- **iPhone and iPad:** Safari 17.1 or newer, tier 1, on MSE (`ManagedMediaSource`). Safari 16.4 to 17.0 is
  unsupported. Chrome on iOS is noted under Safari, since it uses the same engine.
- **Android:** Chrome tier 1. Samsung Internet and Firefox tier 3.
- **TV browsers:** Google TV and Android TV WebView, tier 2, on a Google TV Streamer. Tizen 8 and newer and webOS 24
  and newer (Chromium 108) are tier 3 until a reference TV exists. Whether the maintainer owns one is unverified.
  Hosted apps only.
- WASM SIMD is required, and there is no non-SIMD build. Threads are optional (D-17,
  [ADR-0037](0037-wasm-threading-and-cross-origin-isolation.md)).
- Memory profiles: `default` and `constrained`. Heap ceilings come from measurement under #145, not from this ADR.

### Desktop

- macOS 14 or newer.
- Ubuntu 26.04 LTS on x86-64 and arm64, plus current Fedora. Ubuntu 24.04 is unsupported, since ADR-0013 needs
  GCC 15.
- Wayland and X11 both. CI is GPU-less and uses lavapipe.
- Windows 11 x64: "engine builds, not supported" (ADR-0031).

### Apple

- iOS, iPadOS and tvOS 17, and macOS 14. This gives one renderer API (`AVSampleBufferVideoRenderer`), matches the
  Safari 17.1 web floor, and unlocks `std::format` and `std::print` under ADR-0013. It drops iPhones that cannot run
  17, about iPhone 8 and X (unverified).

### Android

- `minSdk` **28** (Android 9). This keeps one decode path and makes the NDK async `AMediaCodec` API (API 28) available, although D-21
  (#21) starts with Java async `MediaCodec`
  through JNI. `Surface.setFrameRate` (API 30) is used where present.
- **Pre-approved move to 30** (Android 11): if supporting API 28 and 29 becomes a burden during M6, raising the
  floor to 30 is already approved. It then needs only a matrix change, a release note and a reference in the D-21
  work, with no new ADR. It guarantees frame-rate matching, and may exclude some Google TV hardware (Chromecast with
  Google TV ships Android 10; unverified).
- This also answers D-21 Q1. IMA's Android floor (minSdk 24) is satisfied.

### Toolchains

Toolchain pins are copied from ADR-0013. Updating a pin needs CI probe evidence.

## Consequences

**Positive**
- One decode and renderer path per platform, and a clear answer for D-18, D-20 and D-21.
- The matrix is honest: every cell links to evidence.
- Changing a cell or the Android floor within the pre-approval is cheap.

**Negative / costs**
- Some older iPhones, and Android devices on versions 8 and earlier, are excluded.
- TV tiers stay tier 3 until a reference TV exists.
- A data file and its generator are a new thing to maintain.

**Follow-ups**
- #156 runs cross-browser conformance against the tier 1 web cells.
- #181, #183 and #198 (M6) use `minSdk` 28. D-21 (#21) builds on it.
- #216 (M7) uses the Apple floors, and D-20 (#20) can assume one renderer API.
- #268 (M11) reads the matrix for release testing.
- New task: create the matrix data file, its generator and the evidence-link check, and replace the hand-written
  tables in `docs/compatibility.md`.
- New task: probe `std::format` and `std::print` on the Apple 17 and 14 deployment targets (ADR-0013 process).
- Update `docs/compatibility.md` with the floors above, done with this ADR.
- Source a Tizen or webOS reference TV, or confirm none exists.

## Pros and cons of the options

### A — A single list
- Good, because it is simple.
- Bad, because it hides what is verified, and invites claims without evidence.

### B — Tiers with feature floors
- Good, because each cell shows its evidence, and the matrix matches ADR-0021.
- Bad, because it needs a data file and discipline to keep evidence current.

### Apple 17 and 14
- Good, because it has one renderer API and unlocks `std::format`.
- Bad, because it drops the oldest devices.

### Apple 16.3 and 13.3, or 15 and 13
- Good, because they reach more devices.
- Bad, because they need a display-layer fallback or two renderer paths.

### Android 28
- Good, because it keeps a single decode path and makes NDK async `AMediaCodec` available.
- Bad, because it excludes older boxes.

### Android 24
- Good, because it matches IMA's floor.
- Bad, because it means Java `MediaCodec` only on the oldest devices, and more legacy code.

### Android 30
- Good, because it guarantees frame-rate matching.
- Bad, because it excludes some Google TV hardware (unverified).

## References

- [Android API levels and distribution](https://developer.android.com/guide/topics/manifest/uses-sdk-element#ApiLevels)
- [NDK `AMediaCodec` async callbacks](https://developer.android.com/ndk/reference/group/media)
- [`Surface.setFrameRate`](https://developer.android.com/reference/android/view/Surface#setFrameRate(float,%20int))
- [Apple: `AVSampleBufferVideoRenderer`](https://developer.apple.com/documentation/avfoundation/avsamplebuffervideorenderer)
- [Can I use: ManagedMediaSource](https://caniuse.com/mdn-api_managedmediasource)
- [Tizen Web engine versions](https://developer.samsung.com/smarttv/develop/specifications/web-engine-specifications.html)
- [webOS TV web engine versions](https://webostv.developer.lge.com/develop/specifications/web-api-and-web-engine)
- [ADR-0013](0013-cpp-language-standard.md), [ADR-0021](0021-testing-strategy.md),
  [ADR-0030](0030-desktop-graphics-audio-and-ui-renderer.md), [ADR-0031](0031-windows-support-scope-and-toolchain.md)
