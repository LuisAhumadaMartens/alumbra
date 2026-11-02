# 0031 — Build and test the engine on Windows 11 with clang-cl from M1, and defer playback until after M7

- **Status:** Accepted
- **Date:** 2026-11-01
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #35

## Context

[ADR-0006](0006-platform-order.md) defines desktop as macOS and Linux, and no other accepted decision mentions
Windows. Windows is the largest desktop platform, and VLC and mpv both treat it as a core platform, so leaving it
out of a player that aims to play anything, anywhere has to be a deliberate choice. D-35 decides whether Windows is
an Alumbra platform, when each level of support arrives and which compiler builds it.

Two forces pull in opposite directions:

- Portability is cheapest to protect before Umbra code accumulates. If Umbra is built only by GCC and Clang on
  POSIX systems, assumptions that do not hold on Windows creep in unnoticed, and each one becomes a bug on the day
  a Windows build starts.
- A third desktop operating system on M1's critical path would delay the web port, the riskiest one
  ([ADR-0004](0004-web-output-hybrid-mse-webcodecs.md)), which ADR-0006 deliberately brings up second.

Some of the ground is already covered:

- The layering check (`tools/layering/allowlist.json`) limits Umbra to standard, Abseil, Umbra and
  Penumbra-interface headers, so `<pthread.h>` and other OS headers already fail on Linux and macOS.
- [ADR-0021](0021-testing-strategy.md) runs the sanitizer gates on Linux and macOS. ThreadSanitizer does not
  support Windows and UndefinedBehaviorSanitizer support there is partial, so a Windows job can add at most
  AddressSanitizer.
- [ADR-0013](0013-cpp-language-standard.md) allows a C++23 library feature only after its probe passes on every
  pinned toolchain. It left MSVC unprobed until D-35 decided whether Windows is in scope.
- [ADR-0014](0014-dependency-and-licensing-policy.md) sets dependency eligibility. Its desktop rule for LGPL
  libraries, separately replaceable shared libraries, applies to Windows DLLs as it does to the other desktop
  builds.

D-35 gates M1 but blocks no M1 task. Rendering, decoding and packaging on Windows matter only once Windows plays
media, so this ADR settles only what the engine build needs now.

## Decision drivers

- Keep Umbra and the Penumbra interface portable to Windows while that is cheap.
- Keep ADR-0006's order: no third desktop operating system on M1's critical path.
- One compiler family across platforms, producing the ABI that Windows C++ applications use.
- ADR-0013's feature probes cover every toolchain that builds Alumbra.
- A small, fixed cost: one CI job on runners that are free for public repositories.
- Decide now only what M1 needs, and settle playback details when playback is scheduled.

## Considered options

Scope and timing:

1. **A — Engine builds from M1, playback after M7.** CI compiles Umbra and the Penumbra interface and runs the unit
   tests on Windows; a best-effort `alumbra-cli` follows after M7.
2. **B — Out of scope before 1.0.** No Windows builds.
3. **C — Best-effort playback in M1.** As A, plus a smoke-tested desktop port and an `alumbra-cli` archive marked
   best effort.
4. **D — First-class in M1.** Windows joins macOS and Linux as a reference platform with full conformance.

Compiler:

1. **A — clang-cl with the MSVC STL.** Clang's MSVC-compatible driver, producing the MSVC ABI.
2. **B — MSVC (`cl.exe`).**
3. **C — Both clang-cl and MSVC.**
4. **D — llvm-mingw (Clang with libc++),** producing the MinGW ABI.

## Decision

Chosen options: **scope A** and **compiler A**. One CI job keeps the engine portable without changing ADR-0006's
order, and clang-cl keeps Clang on every platform while producing the MSVC ABI that Windows applications link
against.

### Scope and timing

- From M1, CI builds Umbra and the Penumbra interface on Windows and runs their unit tests, using ADR-0021's fakes
  and virtual time. No Windows Penumbra port is built, and Windows has no user-facing support yet.
- A best-effort `alumbra-cli` for Windows, with software decoding, is a roadmap item after M7 and before 1.0. It is
  scheduled once the four reference ports exist and the port contract has settled.
- Windows does not join M1's exit criteria, or the conformance requirements of any milestone before that item.

### Windows versions and architectures

- Windows 11 is the only supported Windows version.
- x64 is built in CI from M1. ARM64 is built once playback lands; GitHub provides Windows 11 ARM64 runners for
  public repositories.
- GitHub's hosted x64 Windows runners run Windows Server, not Windows 11. A compile and unit-test job exercises the
  toolchain, the standard library and the engine, not client-only features of the operating system, so a pinned
  Windows Server image is enough for it. Testing on Windows 11 itself starts with playback.

### Toolchain

- Alumbra's own targets are built on Windows with clang-cl, using the MSVC STL and the MSVC ABI. Only this pair is
  pinned and probed; `cl.exe` and MinGW toolchains are not Alumbra build toolchains.
- The exact versions, an LLVM release for clang-cl and an MSVC toolset for its STL, are to be pinned by the CI
  task. clang-cl is not pinned below ADR-0013's Clang floor, Clang 21.

### Engine CI job

- One Windows x64 job configures, builds and tests Umbra and the Penumbra interface with no port, including the
  C++ feature probe.
- It is a merge gate under the same path filter as the C++ matrix
  ([ADR-0012](0012-repository-topology-and-packaging.md#decision)) and is aggregated by `ci-ok`.
- Sanitizers on Windows are limited to AddressSanitizer. The CI task may run the unit tests under it in the same
  job, with dependencies instrumented as ADR-0021 requires. UndefinedBehaviorSanitizer and ThreadSanitizer stay on
  Linux and macOS.

### C++ feature probes

- clang-cl with the MSVC STL joins ADR-0013's toolchain floors as one more compiler and standard-library pair, as
  ADR-0013 anticipated when it left MSVC unprobed pending D-35. Its probes therefore gate every C++23 library
  feature on every platform: a feature becomes allowed only once it also passes on this pair.
- `tools/cxx-probe` runs on clang-cl itself, not on `cl.exe`. The MSVC STL's feature availability can depend on
  the compiler, so `cl.exe` results are not evidence for clang-cl.
- Until the pair has probe evidence, it is listed as not probed and keeps every feature pending, as Emscripten and
  the Android NDK already do. No feature is allowed today, so this changes nothing immediately.
- A feature the MSVC STL lacks stays pending, or becomes a toolchain ban, for every platform, under ADR-0013's
  existing rules.

### Dependencies

- Every vcpkg dependency of Umbra, the Penumbra interface, their tests and the desktop port must build for the
  Windows x64 triplet that the CI task defines. A dependency that does not is not adopted.
- A dependency that only another platform's code uses, such as a Linux-only system library, is declared with a
  vcpkg platform expression for that platform and is exempt.
- The Windows job checks the rule. It installs every manifest feature for the triplet, including `desktop`, even
  though it builds and tests only the engine.

### Relationship to accepted decisions

- **ADR-0006.** Its platform scope, desktop as macOS and Linux only, is superseded in part: desktop now includes
  Windows 11 at the levels above. Its platform order and its macOS and Linux reference port stand. Accepted ADRs
  are immutable ([ADR-0001](0001-record-architecture-decisions.md), [process](../process.md)), so this ADR
  supersedes that scope instead of amending ADR-0006 as the definition issue proposed, and ADR-0006's status line
  says so, as [ADR-0009](0009-toolchain.md)'s does for ADR-0013.
- **ADR-0013.** This ADR adds a floor pair, which ADR-0013 left open for D-35. ADR-0013's rules are unchanged, so it
  is not superseded.
- **ADR-0014.** The Windows triplet rule is a new eligibility condition. ADR-0014 cannot be edited, so the rule is
  recorded here. It adds to ADR-0014's conditions and relaxes none. On Windows, LGPL libraries such as FFmpeg
  follow ADR-0014's desktop row and ship as separately replaceable DLLs.
- **ADR-0021.** Windows adds at most AddressSanitizer and changes none of the Linux and macOS gates.
- **Layering.** The allowlist already keeps `<pthread.h>` and other OS headers out of Umbra. This ADR adds no
  coding rules for Umbra; the Windows job is the check.

### Deferred to the Windows playback milestone

These choices matter only once Windows plays media. They are recorded as the default direction, so that M1 work
does not close them off, but they are provisional, not binding decisions. They are confirmed or changed when the
Windows playback milestone is scheduled.

- **Rendering:** follow D-19 (ADR-0030): SDL_GPU on Direct3D 12, with the same HLSL shaders compiled to DXIL.
- **Decoding:** FFmpeg, in software first and then with D3D11VA or D3D12VA hardware decoding. No Media Foundation.
- **Packaging:** a release zip for the best-effort tier, and a winget manifest once the tier is promoted, through
  D-34 (#34).
- **Also settled then:** the milestone's exit criteria, the minimum Windows 11 version in the compatibility
  matrix (D-22, #22), Windows support for embedders, such as compiling the public headers with `cl.exe`, and the
  ARM64 job.

## Consequences

**Positive**

- POSIX-only assumptions in Umbra and the Penumbra interface fail CI as they appear, not when Windows playback
  starts.
- ADR-0006's order and M1's scope are unchanged, and Windows playback becomes a scheduled step rather than a
  retrofit.
- Clang diagnostics on every platform, and Windows output that links with MSVC-built applications and with
  vcpkg's Windows packages.
- ADR-0013's policy is verified on the Windows toolchain by the same probes as everywhere else.

**Negative / costs**

- One more CI job on every C++ change. Windows runners are slower than Linux ones, and a cold vcpkg cache that
  includes FFmpeg is slow to rebuild.
- The MSVC STL becomes one more gate for C++23 library features: a feature it lacks stays pending or banned on
  every platform.
- No ThreadSanitizer or UndefinedBehaviorSanitizer coverage on Windows.
- A library without a working Windows build cannot be adopted for the engine or the desktop port.
- Windows users get no player before the post-M7 roadmap item.

**Follow-ups**

- **Windows engine CI task:** add the Windows x64 job with a pinned runner image, clang-cl and the MSVC STL at
  pinned versions, a Windows overlay triplet that keeps FFmpeg dynamic as ADR-0014 requires, and a preset that
  builds the engine and its tests with no port. The job installs every manifest feature, joins the path filter and
  `ci-ok`, and updates the CI table and toolchain notes in [process](../process.md#continuous-integration).
- **cxx-probe on Windows:** run `tools/cxx-probe` on the pinned clang-cl pair, record its evidence in
  [`docs/cpp-features.md`](../cpp-features.md), replace that page's "MSVC — Not selected" row with the clang-cl and
  MSVC STL floor, and add the pair to the configure-time floor check in `cmake/AlumbraToolchain.cmake`.
- **Roadmap:** add the best-effort Windows `alumbra-cli` item, with software decoding, after M7 and before 1.0, when
  it is scheduled. Its definition confirms or changes the deferred defaults above.
- **D-22 (#22):** the compatibility matrix lists Windows 11 x64 as "engine builds, not supported" until playback
  lands, and records the clang-cl and MSVC STL floor with the other toolchain floors.
- ADR-0006's status line notes that its platform scope is superseded in part by this ADR; its order and reference
  port stand.

## Pros and cons of the options

### Scope A. Engine builds from M1, playback after M7

- Good, because one CI job protects portability from the first line of engine code.
- Good, because ADR-0006's order and M1's scope are unchanged.
- Bad, because Windows users wait until after M7 for a player.

### Scope B. Out of scope before 1.0

- Good, because it costs nothing now.
- Bad, because nothing checks portability, and Windows support would have to be retrofitted.

### Scope C. Best-effort playback in M1

- Good, because it gives a credible Windows story early.
- Bad, because it adds moderate scope to M1 and meets port quirks before the port contract settles.

### Scope D. First-class in M1

- Good, because it gives the widest reach.
- Bad, because it multiplies M1's checks and puts a third desktop operating system ahead of the web port.

### Compiler A. clang-cl with the MSVC STL

- Good, because it produces the MSVC ABI that Windows applications use and links with vcpkg's Windows packages.
- Good, because it keeps one compiler family and consistent diagnostics; Chromium also builds Windows with clang-cl.
- Bad, because the MSVC STL becomes a new gate for C++23 library features.

### Compiler B. MSVC (`cl.exe`)

- Good, because it is the default Windows toolchain.
- Bad, because it brings a different front end, warnings and extensions, with the same standard-library gate.

### Compiler C. Both clang-cl and MSVC

- Good, because it gives the most coverage.
- Bad, because it needs two Windows jobs and two probe runs for little extra protection.

### Compiler D. llvm-mingw (Clang with libc++)

- Good, because it uses the same libc++ as macOS and Android, so it adds no new standard-library gate.
- Bad, because the MinGW ABI is incompatible with MSVC-built applications that consume the C++ API, and vcpkg's
  MinGW triplets are community triplets that vcpkg's CI does not test.

## References

- [D-35 discussion](https://github.com/LuisAhumadaMartens/alumbra/issues/35) and
  [D-19: desktop graphics, audio and UI renderer](https://github.com/LuisAhumadaMartens/alumbra/issues/19)
- [ADR-0001](0001-record-architecture-decisions.md), [ADR-0006](0006-platform-order.md),
  [ADR-0009](0009-toolchain.md), [ADR-0012](0012-repository-topology-and-packaging.md),
  [ADR-0013](0013-cpp-language-standard.md), [ADR-0014](0014-dependency-and-licensing-policy.md),
  [ADR-0021](0021-testing-strategy.md)
- [C++ feature policy](../cpp-features.md), [process and current CI](../process.md#continuous-integration)
- [Clang: clang-cl](https://clang.llvm.org/docs/UsersManual.html#clang-cl),
  [Clang: MSVC compatibility](https://clang.llvm.org/docs/MSVCCompatibility.html)
- [Microsoft STL](https://github.com/microsoft/STL),
  [Microsoft C/C++ language conformance](https://learn.microsoft.com/en-us/cpp/overview/visual-cpp-language-conformance),
  [Clang/LLVM support in Visual Studio CMake projects](https://learn.microsoft.com/en-us/cpp/build/clang-support-cmake)
- [vcpkg: triplets](https://learn.microsoft.com/en-us/vcpkg/concepts/triplets),
  [vcpkg: triplet variables](https://learn.microsoft.com/en-us/vcpkg/users/triplets)
- [GitHub-hosted runners](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)
- [Clang: AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html),
  [MSVC: AddressSanitizer](https://learn.microsoft.com/en-us/cpp/sanitizers/asan),
  [Clang: ThreadSanitizer supported platforms](https://clang.llvm.org/docs/ThreadSanitizer.html#supported-platforms)
- [Chromium: Windows build instructions](https://chromium.googlesource.com/chromium/src/+/main/docs/windows_build_instructions.md)
- Provisional playback defaults: [SDL3 GPU API](https://wiki.libsdl.org/SDL3/CategoryGPU),
  [FFmpeg: hardware acceleration](https://trac.ffmpeg.org/wiki/HWAccelIntro),
  [Windows Package Manager](https://learn.microsoft.com/en-us/windows/package-manager/)
