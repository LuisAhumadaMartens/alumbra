# 0013 — Build with C++23, keep public headers at C++20, allow library features one by one

- **Status:** Proposed
- **Date:** 2026-10-31
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #274

> This draft records the recommendation of [D-36](https://github.com/LuisAhumadaMartens/alumbra/issues/274) so
> it can be read next to the debate. When the debate converges, it is revised and accepted, or dropped, through
> its pull request.

## Context

[ADR-0009](0009-toolchain.md) set the language to C++20, in line with the
[Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html#C++_Version) ("code should target
C++20, i.e., should not use C++23 features"). Before engine code lands, D-36 rechecked that choice against the
standards and toolchains as of 2026-10-31:

- C++23 (ISO/IEC 14882:2024) is the newest published standard. The C++26 DIS ballot closed on
  23 October 2026 without approval, so C++26 is not available.
- Chromium has allowed C++23 feature by feature since January 2026. Cobalt 27 LTS, based on Chromium M138, is
  still C++20.
- The C++23 language mode works on every toolchain Alumbra targets, but the standard libraries differ.
  Twenty-one single-feature probes, run on Apple libc++ (Xcode 27.2, including iOS/tvOS 15 deployment), libc++
  21/22, Emscripten 6.0.13, GCC 13.3/14.2/15.2 and Clang 18, show a portable subset and a set of gaps (the
  table is in D-36).
- Floating-point `std::format` and `std::print` need iOS/tvOS 16.3 and macOS 13.3, even in C++20 mode.
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
4. **Wait for C++26**, rejected: no approved draft exists.

## Decision

Chosen option: **2**, because it uses the standard library where every pinned toolchain supports it, keeps
the embedder contract at C++20, and follows Chromium's allowlist model.

- **Build:** Alumbra's own targets use `CMAKE_CXX_STANDARD 23`, `CMAKE_CXX_STANDARD_REQUIRED ON` and
  `CMAKE_CXX_EXTENSIONS OFF`.
- **Public headers:** C++20. The package keeps `target_compile_features(... PUBLIC cxx_std_20)`, and the
  installed-consumer test compiles every public header at C++20.
- **Allowed:** deducing `this`, `if consteval`, multidimensional `operator[]`, `std::expected`,
  `std::byteswap`, `std::to_underlying`, `std::unreachable`, `std::out_ptr`, `std::ranges::to`,
  `std::views::zip`, `std::flat_map`.
- **Allowed if the Apple floor (D-22) is iOS/tvOS 16.3 and macOS 13.3 or later:** `std::print` and
  floating-point `std::format`; otherwise `absl::StrFormat`.
- **Banned until every pinned toolchain has them:** `std::mdspan`, `std::move_only_function` (use
  `absl::AnyInvocable`), `std::generator`, `std::views::enumerate`, `std::views::chunk`, `std::stacktrace`.
- **Banned by policy:** `[[assume]]`.
- **Out of scope:** C++ modules, including `import std`.
- **Toolchain floors:** GCC 15, Clang 21, Xcode 26.6 (confirmed by CI probes), Emscripten 6, Android NDK r30.

This amends the language clause of ADR-0009 only. Its build system, style, library and test decisions stand.

## Consequences

**Positive**
- `std::expected`, `std::flat_map` and `std::ranges::to` are usable from the first engine code, without
  waiting for the style guide.
- Embedders still need only C++20.
- The policy is executable: probes and a ban-list check fail CI instead of relying on review.

**Negative / costs**
- A documented deviation from the Google C++ Style Guide.
- Ubuntu 24.04's default compilers (GCC 13, Clang 18) cannot build Alumbra; the Linux floor is Ubuntu 26.04 or
  an upgraded compiler.
- Two language levels to keep apart: implementation files may use the allowlist, public headers may not.

**Follow-ups**
- `docs/cpp-features.md`, modelled on Chromium's `c++-features.md`, lists allowed, banned and pending features.
- `tools/cxx-probe/` holds the probes; the CI workflow runs them on every pinned toolchain.
- The clang-format and clang-tidy gate rejects banned features.
- D-22 records the toolchain floors in the compatibility matrix.
- On acceptance, ADR-0009's status line notes that its language clause is superseded by this ADR.

## References

- [D-36: C++ language standard and library feature policy](https://github.com/LuisAhumadaMartens/alumbra/issues/274)
- [Google C++ Style Guide: C++ version](https://google.github.io/styleguide/cppguide.html#C++_Version)
- [Chromium: Modern C++ use in Chromium](https://chromium.googlesource.com/chromium/src/+/main/styleguide/c++/c++-features.md)
- [Train IT: The result of the C++26 DIS ballot](https://train-it.eu/blog/cpp26-contracts-the-ballot-result/)
- [cppreference: C++23 compiler support](https://en.cppreference.com/w/cpp/compiler_support/23)
- [libc++: C++23 status](https://libcxx.llvm.org/Status/Cxx23.html)
- [libstdc++: C++23 status](https://gcc.gnu.org/onlinedocs/libstdc++/manual/status.html#status.iso.2023)
- [ADR-0009: Toolchain](0009-toolchain.md)
