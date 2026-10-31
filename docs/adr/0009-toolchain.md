# 0009 — Toolchain: C++20, CMake presets, vcpkg, Google C++ Style, GoogleTest, Abseil

- **Status:** Accepted
- **Date:** 2026-10-31
- **Deciders:** @LuisAhumadaMartens

## Context

Alumbra targets six build environments: macOS, Linux, Emscripten (web), Android NDK (Android/Google TV), and
Xcode (iOS/iPadOS/tvOS). The build must be reproducible, cross-compile cleanly and integrate with each platform's
native tooling (Gradle, Xcode, npm).

## Decision drivers

- First-class paths into Android Gradle (`externalNativeBuild`), Xcode, and Emscripten.
- Reproducible third-party dependencies with triplets per platform.
- Code that reads like Google C++.

## Considered options

1. **C++20 + CMake (presets) + vcpkg** (manifest mode).
2. C++20 + Bazel (Bzlmod).
3. C++20 + GN/Ninja (Chromium/Cobalt-faithful).

## Decision

Chosen option: **1.**

- **Language:** C++20 (no exceptions across the public C++ API boundary; error handling via `absl::Status` /
  `absl::StatusOr` — to be confirmed in D-09).
- **Build:** CMake ≥ 3.28 with `CMakePresets.json` per platform; vcpkg manifest with per-platform triplets.
- **Style & lint:** [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html), clang-format
  (`BasedOnStyle: Google`), clang-tidy.
- **Libraries & tests:** Abseil, GoogleTest, Google Benchmark; sanitizers (ASan, UBSan, TSan) in CI;
  libFuzzer for parsers.

## Consequences

**Positive**
- Broad ecosystem; minimal friction with mobile and web toolchains.

**Negative / costs**
- Less hermetic than Bazel; mitigated by presets, pinned vcpkg baseline and CI caching.
