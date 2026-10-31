# 0013 — Build with C++23, keep public headers at C++20, allow library features one by one

- **Status:** Accepted
- **Date:** 2026-10-31
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #274

## Context

[ADR-0009](0009-toolchain.md) set the language to C++20, in line with the
[Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html#C++_Version) ("code should target
C++20, i.e., should not use C++23 features"). Before engine code lands, D-36 rechecked that choice against the
standards and toolchains as of 2026-10-31:

- C++23 (ISO/IEC 14882:2024) is the newest published standard. The C++26 DIS ballot closed on
  23 October 2026 without approval, so C++26 remains unpublished; working drafts and experimental compiler
  modes exist, but are not a published standard.
- Chromium has allowed C++23 feature by feature since January 2026. Cobalt 27 LTS, based on Chromium M138, is
  still C++20.
- The measured toolchains support C++23 language mode, but their standard libraries differ.
  Twenty-one single-feature probes, run on Apple libc++ (Xcode 27.2, including iOS/tvOS 15 deployment), libc++
  21/22, Emscripten 6.0.13, GCC 13.3/14.2/15.2 and Clang 18, show a portable subset and a set of gaps (the
  table is in D-36). Only Xcode 27.2 was measured; Xcode 26.6, Android NDK and MSVC were not directly probed.
- Floating-point `std::format` is a C++20 library feature; `std::print` requires C++23. On Apple platforms,
  both need iOS/tvOS 16.3 and macOS 13.3.
- Embedders (Gradle, Xcode with Swift–C++ interoperability, Emscripten, CMake `find_package`) compile the
  public headers with their own language setting.

## Decision drivers

- One language policy that every target toolchain can honour, verified by CI rather than by memory.
- No new language requirement for embedders.
- Standard vocabulary types where they are portable; Abseil where they are not.
- Reviewers from Google and Chromium should recognise the policy and its deviation from the style guide.

## Considered options

1. **C++20 everywhere** (ADR-0009 as written), with Abseil filling the gaps.
2. **C++23 build, C++20 public headers, per-feature allowlist.**
3. **C++23 everywhere**, including public headers.
4. **Wait for C++26**, rejected: it is not yet a published standard.

## Decision

Chosen option: **2**, because it uses the standard library only where every selected toolchain is validated to
support it, keeps the embedder contract at C++20, and follows Chromium's allowlist model. Alumbra's own targets
use `CMAKE_CXX_STANDARD 23`, `CMAKE_CXX_STANDARD_REQUIRED ON` and
  `CMAKE_CXX_EXTENSIONS OFF`.
- **Public headers:** C++20. The package keeps `target_compile_features(... PUBLIC cxx_std_20)`, and the
  installed-consumer test compiles every public header at C++20.
- **Initial feature candidates (pending):** deducing `this`, `if consteval`, multidimensional `operator[]`,
  `std::expected`, `std::byteswap`, `std::to_underlying`, `std::unreachable`, `std::out_ptr`,
  `std::ranges::to`, `std::views::zip`, `std::flat_map`. None is usable merely because this ADR is accepted:
  each remains pending until exact-version probe evidence passes on every pinned toolchain and a reviewed
  change moves it to allowed in `docs/cpp-features.md`.
- **Apple-floor-gated features:** `std::print` (C++23) and floating-point `std::format` (C++20) may be used
  only if D-22 (#22) sets the Apple floors at iOS/tvOS 16.3 and macOS 13.3 or later, and their probes pass on
  every pinned toolchain. Until then, use `absl::StrFormat`.
- **Banned until every pinned toolchain has them:** `std::mdspan`, `std::move_only_function` (use
  `absl::AnyInvocable`), `std::generator`, `std::views::enumerate`, `std::views::chunk`, `std::stacktrace`.
- **Banned by policy:** `[[assume]]`.
- **Out of scope:** C++ modules, including `import std`.
- **Toolchain floors:** GCC 15, Clang 21, Xcode 26.6, Emscripten 6, Android NDK r30. A feature may be used
  only after exact-version probe evidence passes on every pinned toolchain; compiler-major proxies are not
  sufficient. Xcode 26.6 and Android NDK r30 were not probed; MSVC was not probed and requires verification if
  Windows is selected under D-35 (#35). If a probe fails at a floor, that feature remains pending and may not
  be used.
- **Executable policy:** `docs/cpp-features.md` is the living list of allowed, pending and banned features.
  Moving a feature between those states requires a reviewed pull request with probe evidence, not a new ADR.
  Changing this rule requires a superseding ADR, because accepted ADRs are immutable.

## Consequences

**Positive**
- `std::expected`, `std::flat_map` and `std::ranges::to` may be used once their probes pass on every pinned
  toolchain, without waiting for the style guide.
- Embedders still need only C++20.
- The policy has executable acceptance criteria: probes on every pinned toolchain and a ban-list check in CI.

**Negative / costs**
- A documented deviation from the Google C++ Style Guide.
- Ubuntu 24.04's default compilers (GCC 13, Clang 18) cannot build Alumbra; the Linux floor is Ubuntu 26.04 or
  an upgraded compiler.
- Two language levels to keep apart: implementation files may use allowed features, public headers may not.

**Follow-ups**
- #276, **Adopt the C++23 build mode and executable feature policy**: apply the build switch, add a
  configure-time floor check, and add `tools/cxx-probe/` and `docs/cpp-features.md`.
- #42: CI validates the desktop probe matrix on every pinned desktop toolchain. Future platform CI validates
  each other pinned target before its probe evidence can move a feature to globally allowed.
- #43: the lint gate rejects banned features.
- D-22 (#22): records the toolchain floors in the compatibility matrix.
- ADR-0009's status line notes that its language clause is superseded by this ADR.

## References

- [D-36: C++ language standard and library feature policy](https://github.com/LuisAhumadaMartens/alumbra/issues/274)
- [Google C++ Style Guide: C++ version](https://google.github.io/styleguide/cppguide.html#C++_Version)
- [Chromium: Modern C++ use in Chromium](https://chromium.googlesource.com/chromium/src/+/main/styleguide/c++/c++-features.md)
- [Train IT: The result of the C++26 DIS ballot](https://train-it.eu/blog/cpp26-contracts-the-ballot-result/)
- [cppreference: C++23 compiler support](https://en.cppreference.com/w/cpp/compiler_support/23)
- [libc++: C++23 status](https://libcxx.llvm.org/Status/Cxx23.html)
- [libstdc++: C++23 status](https://gcc.gnu.org/onlinedocs/libstdc++/manual/status.html#status.iso.2023)
- [ADR-0009: Toolchain](0009-toolchain.md)
