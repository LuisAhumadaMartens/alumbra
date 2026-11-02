# 0038 — Bind every SDK through one public C ABI that stays experimental until 1.0

- **Status:** Accepted
- **Date:** 2026-11-01
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #18

## Context

Antumbra builds TypeScript, Kotlin and Swift SDKs on Umbra, and Umbra's public API is C++ ([ADR-0023](0023-public-api-surface-and-media-error.md)).
All three SDKs need one binding strategy, which is expensive to change after SDKs ship. D-18 gates M4, M6 and M7
and blocks #144 first.

Accepted decisions bound it:

- [ADR-0016](0016-penumbra-port-contract.md) says "a C ABI may be added later". This definition is that step, kept
  separate from the C++ Penumbra port contract.
- [ADR-0015](0015-threading-and-task-runners.md) raises events by "notify, then pull". The issue's "callbacks on an
  arbitrary Umbra thread" conflicts with it.
- [ADR-0017](0017-settings-schema-and-configuration.md)'s generator can emit the mechanical parts, and
  [ADR-0018](0018-error-model-and-recovery.md) fixes the error catalogue.
- [ADR-0012](0012-repository-topology-and-packaging.md) sets one version for every package.

## Decision drivers

- One boundary to maintain across web, Android and Apple.
- Idiomatic SDKs for each language, written by hand where behaviour matters.
- Symbols that stay stable across releases once an AAR or XCFramework ships (D-34).
- Honest maintenance cost, and room for third parties to bind other languages.
- Few dependencies in the glue layers.

## Considered options

1. **A — Language-specific bindings** straight onto C++ (embind, JNI over C++, Swift–C++ interop).
2. **B — One C ABI**, with handwritten idiomatic SDKs on top and generated mechanical parts.
3. **C — A generated binding layer** from an own IDL.

C ABI visibility:

1. **Public, experimental** until 1.0, then covered by SemVer.
2. **Private to the SDKs**, free to change until after 1.0.

## Decision

Chosen options: **B, with a public C ABI that is experimental until 1.0.** Documented and versioned, it lets third
parties bind other languages and keeps SDK maintenance honest. The cost is documentation upkeep. After 1.0 it falls
under SemVer ([ADR-0041](0041-releases-and-package-publication.md)).

### The boundary

- Names start with `alumbra_`. An API version constant is exposed. Extensible structs begin with `struct_size`.
  Deprecations are kept for one minor release, as in ADR-0023.
- The generator from ADR-0017 emits enums, settings accessors, event payloads and the error catalogue. Behaviour is
  written by hand.
- **Events.** The ABI exposes the ADR-0015 notify hook and a pull call. The notify hook may fire on any thread and
  carries no payload. Each SDK drains on its own main thread: the JS event loop, Android's main looper, or the Swift
  main actor. No payload callback runs on an arbitrary thread.
- **Handles.** Handles are opaque, generation-checked ids, not raw pointers. A released handle returns a defined
  error code. A player has a single owner. Only shared objects, such as tracks and events, are reference-counted.
- **Errors.** Each call returns a code, with an `ErrorDetail` from the catalogue. Kotlin throws only for programmer
  errors and delivers playback errors as events. Swift uses one `AlumbraError` type.

### Per language

- **Web.** Plain exports with thin generated glue. Embind is used only for debug tools. A size and call-overhead
  budget is measured before #144 freezes the layer.
- **Android.** A handwritten, minimal JNI layer, with no fbjni. The `Surface` goes through
  `ANativeWindow_fromSurface`.
- **Apple.** Swift imports the C module directly, with no Swift–C++ interop.
- **C++.** The C++ SDK uses Umbra's C++ API directly (ADR-0023), not the ABI.

## Consequences

**Positive**
- One boundary serves three SDKs and third-party bindings.
- A single event model stays true to ADR-0015.
- No dependency on Swift–C++ interop or fbjni.

**Negative / costs**
- A documented ABI needs upkeep, even while experimental.
- Handwritten SDK layers cost effort in each language.
- The generator grows to cover event payloads and the error catalogue.

**Follow-ups**
- #144 builds the JavaScript bindings and #153 the TypeScript SDK, after measuring the web size and call-overhead
  budget.
- #181, #182 and #191 (M6) build the Android JNI layer and Kotlin SDK. #200, #201 and #209 (M7) build the Apple
  layers.
- New task: define the C ABI header, the version constant, handle scheme and the `struct_size` rules, with a
  documented contract and an `experimental` label in the API reference.
- New task: extend the ADR-0017 generator to emit the ABI enums, event payloads and the error catalogue.
- New task: ABI stability checks in CI (symbol list diff) to start enforcing at 1.0.
- D-34 treats the ABI as SemVer-covered after 1.0 (ADR-0041).

## Pros and cons of the options

### A — Language-specific bindings
- Good, because each one is idiomatic by construction.
- Bad, because there are three bindings to design and keep in step, and C++ interop is required of Swift clients.

### B — One C ABI
- Good, because it is one boundary, with stable symbols.
- Bad, because the C layer must be designed and documented, and each SDK is handwritten above it.

### C — A generated layer from an own IDL
- Good, because it removes handwritten glue.
- Bad, because it needs an own IDL and generator, and behaviour still needs handwriting.

### Public, experimental
- Good, because third parties can bind other languages, and it keeps maintenance honest.
- Bad, because documentation costs upkeep.

### Private to the SDKs
- Good, because it is quicker, and free to change.
- Bad, because third parties must wrap C++, and going public later costs a documentation and freeze pass.

## References

- [Emscripten embind](https://emscripten.org/docs/porting/connecting_cpp_and_javascript/embind.html)
- [JNI specification](https://docs.oracle.com/en/java/javase/21/docs/specs/jni/index.html)
- [Android NDK `ANativeWindow`](https://developer.android.com/ndk/reference/group/a-native-window)
- [Swift: importing C into Swift](https://developer.apple.com/documentation/swift/imported-c-and-objective-c-apis)
- [ADR-0012](0012-repository-topology-and-packaging.md), [ADR-0015](0015-threading-and-task-runners.md),
  [ADR-0016](0016-penumbra-port-contract.md), [ADR-0017](0017-settings-schema-and-configuration.md),
  [ADR-0018](0018-error-model-and-recovery.md), [ADR-0023](0023-public-api-surface-and-media-error.md)
