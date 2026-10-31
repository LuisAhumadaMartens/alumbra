# C++ feature policy

This page implements [ADR-0013](adr/0013-cpp-language-standard.md). Alumbra's own targets build as **C++23**;
installed public headers stay **C++20**, so embedders need only a C++20 compiler. Like Chromium's
[Modern C++ use in Chromium](https://chromium.googlesource.com/chromium/src/+/main/styleguide/c++/c++-features.md),
every C++23 feature has one of three states:

| Status | Meaning |
|--------|---------|
| **allowed** | May be used in implementation files. Public headers still may not use C++23 features. |
| **pending** | Must not be used yet. Every C++23 feature that this page does not list is pending. |
| **banned** | Must not be used. The table gives the reason and the alternative. |

## Policy and capability

A status is **policy**; a probe result is **capability evidence**. They are kept apart on purpose:

- A feature becomes allowed only through a reviewed pull request that changes its row here and shows passing
  probe evidence for **every pinned toolchain**. A toolchain without evidence keeps the feature pending.
- A passing probe never lifts a ban. `[[assume]]` is banned by policy even though every toolchain compiles it.
- Toolchain bans (features some pinned standard library lacks) are lifted the same way as pending features, once
  every pinned toolchain passes.

`tools/cxx-probe` reads the **Probed features** table below, so this page is the executable policy. In a probe
run, an allowed feature that fails fails the run; pending and banned features are only reported.

No C++23 feature is allowed today: Emscripten 6 and Android NDK r30 have no probe evidence yet. Their
platform-enablement tasks (#143 for Emscripten, #181 for the NDK) add it.

## Pinned toolchains

**Requested** floors are the ADR-0013 floors. Configuration checks the compiler, its standard library and, on
Apple, the Xcode or Command Line Tools version (`cmake/AlumbraToolchain.cmake`) and rejects anything older.
**Verified** lists the exact toolchains whose probe results are recorded under [Probe evidence](#probe-evidence).

| Requested floor | Verified with | Status |
|-----------------|---------------|--------|
| GCC 15 with libstdc++ 15 | GCC 15.2.0, Ubuntu 26.04 arm64 | Probed |
| Clang 21 with libstdc++ 15 | Clang 21.1.8 with libstdc++ 15, Ubuntu 26.04 arm64 | Probed |
| Clang 21 with libc++ 21 | Clang 21.1.8 with libc++ 21.1.8, Ubuntu 26.04 arm64 | Probed |
| Xcode 26.6 with Apple libc++ | Command Line Tools 26.6 (Apple Clang 21.0.0, libc++ 21.1.6) and Xcode 27.2, macOS arm64 | Xcode 26.6 itself not probed |
| Emscripten 6 with its libc++ | — | Not probed (#143) |
| Android NDK r30 with its libc++ | — | Not probed (#181) |
| MSVC | — | Not selected; depends on D-35 (#35) |

Linux x86-64 and tvOS are not probed yet either. The only Apple mobile run so far is an iOS 15 compile/link run,
which the result matrix records as `"mode": "compile-link"`.

## Probed features

| Feature | Probe | Status | Paper | Reason / alternative |
|---------|-------|--------|-------|----------------------|
| Deducing `this` (explicit object parameters) | `explicit_this` | pending | [P0847](https://wg21.link/P0847) | ADR-0013 candidate. |
| `if consteval` | `if_consteval` | pending | [P1938](https://wg21.link/P1938) | ADR-0013 candidate. |
| Multidimensional `operator[]` | `multidimensional_subscript` | pending | [P2128](https://wg21.link/P2128) | ADR-0013 candidate. |
| `std::expected`, including monadic operations | `expected` | pending | [P0323](https://wg21.link/P0323), [P2505](https://wg21.link/P2505) | ADR-0013 candidate. Whether errors use `std::expected` or `absl::StatusOr` is D-09 (#9)'s decision. |
| `std::byteswap` | `byteswap` | pending | [P1272](https://wg21.link/P1272) | ADR-0013 candidate. |
| `std::to_underlying` | `to_underlying` | pending | [P1682](https://wg21.link/P1682) | ADR-0013 candidate. |
| `std::unreachable` | `unreachable` | pending | [P0627](https://wg21.link/P0627) | ADR-0013 candidate. |
| `std::out_ptr`, `std::inout_ptr` | `out_ptr` | pending | [P1132](https://wg21.link/P1132) | ADR-0013 candidate. |
| `std::ranges::to` | `ranges_to` | pending | [P1206](https://wg21.link/P1206) | ADR-0013 candidate. |
| `std::views::zip` | `views_zip` | pending | [P2321](https://wg21.link/P2321) | ADR-0013 candidate. |
| `std::flat_map` | `flat_map` | pending | [P0429](https://wg21.link/P0429) | ADR-0013 candidate. |
| `std::flat_set` | `flat_set` | pending | [P1222](https://wg21.link/P1222) | Probed as D-36 proposed; not an ADR-0013 candidate yet. |
| `std::print`, `std::println` | `print` | pending | [P2093](https://wg21.link/P2093) | Gated: only if D-22 (#22) sets Apple floors at iOS/tvOS 16.3 and macOS 13.3 or later. Use `absl::StrFormat`. |
| Floating-point `std::format` (C++20) | `format_floating_point` | pending | [P0645](https://wg21.link/P0645) | Same Apple gate as `std::print`. Use `absl::StrFormat`. |
| `std::mdspan` | `mdspan` | banned | [P0009](https://wg21.link/P0009) | Toolchain ban: libstdc++ 15 lacks it. |
| `std::move_only_function` | `move_only_function` | banned | [P0288](https://wg21.link/P0288) | Toolchain ban: libc++ lacks it. Use `absl::AnyInvocable`. |
| `std::generator` | `generator` | banned | [P2502](https://wg21.link/P2502) | Toolchain ban: libc++ lacks it. |
| `std::views::enumerate` | `views_enumerate` | banned | [P2164](https://wg21.link/P2164) | Toolchain ban: libc++ lacks it. |
| `std::views::chunk` | `views_chunk` | banned | [P2442](https://wg21.link/P2442) | Toolchain ban: libc++ lacks it. |
| `std::stacktrace` | `stacktrace` | banned | [P0881](https://wg21.link/P0881) | Toolchain ban: no portable implementation; libstdc++ needs `-lstdc++exp`. |
| `[[assume]]` | `assume` | banned | [P1774](https://wg21.link/P1774) | Policy ban: a wrong assumption is undefined behaviour, and no measured gain justifies it. |

## Other C++23 features

These opt-in features have no probe yet and are **pending**. Adding a probe and a row above is the first step
towards allowing one. C++ modules, including `import std` and `import std.compat`
([P2465](https://wg21.link/P2465)), are **banned**: ADR-0013 puts them out of scope.

Changes that apply automatically in C++23 mode are not opt-in and are not classified, for example simpler
implicit move ([P2266](https://wg21.link/P2266)), lifetime extension in range-based `for` initialisers
([P2718](https://wg21.link/P2718)) and removed or deprecated facilities.

**Language**

| Feature | Paper |
|---------|-------|
| `size_t` literal suffixes (`uz`, `z`) | [P0330](https://wg21.link/P0330) |
| `auto(x)` and `auto{x}` decay copies | [P0849](https://wg21.link/P0849) |
| Optional `()` in lambda declarators | [P1102](https://wg21.link/P1102) |
| Attributes on lambdas | [P2173](https://wg21.link/P2173) |
| `static operator()` | [P1169](https://wg21.link/P1169) |
| `static operator[]` | [P2589](https://wg21.link/P2589) |
| `#elifdef`, `#elifndef` | [P2334](https://wg21.link/P2334) |
| `#warning` | [P2437](https://wg21.link/P2437) |
| Alias declarations in init-statements | [P2360](https://wg21.link/P2360) |
| Labels at the end of compound statements | [P2324](https://wg21.link/P2324) |
| Non-literal variables, labels and `goto` in `constexpr` functions | [P2242](https://wg21.link/P2242) |
| `static constexpr` variables in `constexpr` functions | [P2647](https://wg21.link/P2647) |
| Delimited escape sequences | [P2290](https://wg21.link/P2290) |
| Named universal character escapes | [P2071](https://wg21.link/P2071) |
| Extended floating-point types (`<stdfloat>`) | [P1467](https://wg21.link/P1467) |
| Class template argument deduction from inherited constructors | [P2582](https://wg21.link/P2582) |

**Library**

| Feature | Paper |
|---------|-------|
| `contains` for `std::basic_string` and `std::basic_string_view` | [P1679](https://wg21.link/P1679) |
| `std::is_scoped_enum` | [P1048](https://wg21.link/P1048) |
| `std::invoke_r` | [P2136](https://wg21.link/P2136) |
| `std::forward_like` | [P2445](https://wg21.link/P2445) |
| `std::bind_back`, `std::ranges::range_adaptor_closure` | [P2387](https://wg21.link/P2387) |
| Monadic operations for `std::optional` | [P0798](https://wg21.link/P0798) |
| `std::basic_string::resize_and_overwrite` | [P1072](https://wg21.link/P1072) |
| `std::allocate_at_least` | [P0401](https://wg21.link/P0401) |
| `std::start_lifetime_as` | [P2590](https://wg21.link/P2590) |
| `std::reference_constructs_from_temporary`, `std::reference_converts_from_temporary` | [P2255](https://wg21.link/P2255) |
| `<spanstream>` | [P0448](https://wg21.link/P0448) |
| `<stdatomic.h>` compatibility header | [P0943](https://wg21.link/P0943) |
| `constexpr` `std::unique_ptr` | [P2273](https://wg21.link/P2273) |
| `constexpr` `std::bitset` | [P2417](https://wg21.link/P2417) |
| `constexpr` integral `std::to_chars` and `std::from_chars` | [P2291](https://wg21.link/P2291) |
| `constexpr` `<cmath>` and `<cstdlib>` | [P0533](https://wg21.link/P0533) |
| Container construction and insertion from ranges (`std::from_range`) | [P1206](https://wg21.link/P1206) |
| Heterogeneous erasure in associative containers | [P2077](https://wg21.link/P2077) |
| Iterator-pair constructors for `std::stack` and `std::queue` | [P1425](https://wg21.link/P1425) |
| `std::ranges::starts_with`, `std::ranges::ends_with` | [P1659](https://wg21.link/P1659) |
| `std::ranges::contains`, `std::ranges::contains_subrange` | [P2302](https://wg21.link/P2302) |
| `std::ranges::find_last` | [P1223](https://wg21.link/P1223) |
| `std::ranges::fold_left` and related folds | [P2322](https://wg21.link/P2322) |
| `std::ranges::iota`, `std::ranges::shift_left`, `std::ranges::shift_right` | [P2440](https://wg21.link/P2440) |
| `std::views::join_with` | [P2441](https://wg21.link/P2441) |
| `std::views::slide` | [P2442](https://wg21.link/P2442) |
| `std::views::chunk_by` | [P2443](https://wg21.link/P2443) |
| `std::views::adjacent`, `std::views::adjacent_transform`, `std::views::zip_transform` | [P2321](https://wg21.link/P2321) |
| `std::views::as_rvalue` | [P2446](https://wg21.link/P2446) |
| `std::views::as_const` and const iterators from `std::ranges::cbegin` | [P2278](https://wg21.link/P2278) |
| `std::views::repeat` | [P2474](https://wg21.link/P2474) |
| `std::views::stride` | [P1899](https://wg21.link/P1899) |
| `std::views::cartesian_product` | [P2374](https://wg21.link/P2374) |
| Formatting ranges, tuples and container adaptors | [P2286](https://wg21.link/P2286) |
| Formatting `std::thread::id` and `std::stacktrace` | [P2693](https://wg21.link/P2693) |
| `std::tuple` compatibility with tuple-like objects | [P2165](https://wg21.link/P2165) |

## Probe evidence

Results from `tools/cxx-probe` on 2026-10-31. ✅ the probe compiled, linked and (natively) returned 0;
❌ it did not compile or link. The Linux runs used Ubuntu 26.04 containers on arm64; the iOS run is compile/link
only with `CMAKE_OSX_DEPLOYMENT_TARGET=15.0`.

| Probe | Command Line Tools 26.6.0, libc++ 21.1.6, macOS arm64 | Xcode 27.2, libc++ 22.1.6, macOS arm64 | GCC 15.2.0, libstdc++ 15, Linux aarch64 | Clang 21.1.8, libstdc++ 15, Linux aarch64 | Clang 21.1.8, libc++ 21.1.8, Linux aarch64 | Xcode 27.2, libc++ 22.1.6, iOS 15.0 (compile-link) |
|-------|:---:|:---:|:---:|:---:|:---:|:---:|
| `explicit_this` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `if_consteval` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `multidimensional_subscript` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `expected` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `byteswap` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `to_underlying` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `unreachable` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `out_ptr` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `ranges_to` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `views_zip` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `flat_map` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `flat_set` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `print` | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |
| `format_floating_point` | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |
| `mdspan` | ✅ | ✅ | ❌ | ❌ | ✅ | ✅ |
| `move_only_function` | ❌ | ❌ | ✅ | ✅ | ❌ | ❌ |
| `generator` | ❌ | ❌ | ✅ | ✅ | ❌ | ❌ |
| `views_enumerate` | ❌ | ❌ | ✅ | ✅ | ❌ | ❌ |
| `views_chunk` | ❌ | ❌ | ✅ | ✅ | ❌ | ❌ |
| `stacktrace` | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |
| `assume` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

The capability gaps match the bans above: libstdc++ 15 lacks `std::mdspan`; libc++ lacks
`std::move_only_function`, `std::generator`, `std::views::enumerate` and `std::views::chunk`; no default build
links `std::stacktrace`; and an iOS 15 deployment target lacks `std::print` and floating-point `std::format`.

## Running the probes

The `cxx_probe` CTest label runs the probes with the same toolchain settings as the build, plus fixtures that
check the pass/fail rules:

```sh
ctest --preset debug -L cxx_probe --output-on-failure
```

The probe project also runs on its own, with any toolchain file, including cross toolchains that Alumbra
itself cannot build for yet:

```sh
cmake --fresh -S tools/cxx-probe -B build/cxx-probe
cmake --fresh -S tools/cxx-probe -B build/cxx-probe-ios \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
```

Each run writes `cxx-probe-results.json` in its build directory: the compiler, standard library, Apple toolchain
version, `__cplusplus`, target system, processor and architectures, deployment target, the mode (`run` executes
each probe natively; `compile-link` only compiles and links it when cross-compiling) and every feature's status
and result (`supported`, `unsupported` or `run-failed`). Probes compile with `-Wall -Wextra -Wpedantic -Werror`.

## Changing a status

1. Run the probes on every pinned toolchain and keep each `cxx-probe-results.json`.
2. For an unlisted feature, add `tools/cxx-probe/probes/<probe>.cc` (exercise the feature and return 0 on
   success) and a row in **Probed features**.
3. Change the row's status and update **Probe evidence**. Allowed requires `supported` on every pinned toolchain.
4. Open a pull request with the result files attached. Changing the rules on this page requires a superseding
   ADR.
