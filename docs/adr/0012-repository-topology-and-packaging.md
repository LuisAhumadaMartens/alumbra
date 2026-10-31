# 0012 — Keep one repository, ship modular packages per ecosystem, and treat apps as thin reference embedders

- **Status:** Accepted
- **Date:** 2026-10-31
- **Deciders:** @LuisAhumadaMartens

## Context

Alumbra is a player, not a platform ([vision](../vision.md)). One C++ engine (Umbra) runs behind a porting layer
(Penumbra) on desktop, the web, Android and Google TV, and iOS, iPadOS and tvOS. User-facing pieces (Antumbra) wrap it
for each ecosystem: SDKs, UI renderers and apps ([ADR-0002](0002-portable-engine-porting-layer-html5-api.md),
[ADR-0003](0003-layer-naming.md)). That raises three questions:

1. Should the engine, the ports, the SDKs and the apps share one repository, or should each platform app or SDK get
   its own?
2. What is released, and to which package managers?
3. What role do the apps play: products, or hosts for the player?

Comparable projects answer the first question in different ways:

| Project | Topology | Lesson |
|---------|----------|--------|
| [Cobalt](https://github.com/youtube/cobalt) | Engine, Starboard and reference ports in one Chromium-style repository; the YouTube app is not in it | The engine repository holds the power; apps only host it |
| [Media3 / ExoPlayer](https://github.com/androidx/media) | One repository publishing many Maven modules (`exoplayer`, `exoplayer_hls`, `exoplayer_ima`, `ui`, `ui_compose`, …) plus demo apps | One repository can ship many packages; ads are an optional module |
| [Video.js v10](https://github.com/videojs/video.js) | pnpm monorepo: `core`, `html`, `react`, `skins`, …, plus `apps/` and `site/` | Headless core plus thin renderers, the model of [ADR-0007](0007-ui-architecture.md) |
| [Flutter](https://github.com/flutter/engine) | The engine repository was archived and merged into `flutter/flutter` | Split repositories drifted; the merge made cross-cutting changes atomic |
| [VLC](https://code.videolan.org/videolan/vlc) | Core and desktop UIs in one repository; [vlc-android](https://code.videolan.org/videolan/vlc-android) and [vlc-ios](https://code.videolan.org/videolan/vlc-ios) separate | Apps split off once they became products with their own teams and release cycles |
| [mpv](https://github.com/mpv-player/mpv) | Core and libmpv in one repository; GUIs such as [IINA](https://github.com/iina/iina) are third-party | The core is the product; apps are built on it by others |

Package managers also impose constraints:

- **SwiftPM** reads `Package.swift` from the repository root and clones the repository it resolves. Large C++
  repositories therefore usually ship Apple binaries through a small manifest-only repository with
  `.binaryTarget(url:checksum:)`. Google distributes the IMA SDK this way
  ([iOS](https://github.com/googleads/swift-package-manager-google-interactive-media-ads-ios),
  [tvOS](https://github.com/googleads/swift-package-manager-google-interactive-media-ads-tvos)).
- **Homebrew** third-party taps must be repositories named `homebrew-<name>` ([Taps](https://docs.brew.sh/Taps)).

## Decision drivers

- A change to the Penumbra contract must update every port, SDK and sample in one reviewable change.
- The layer dependency rule ([ADR-0003](0003-layer-naming.md)) must be enforced by the build, which needs one tree.
- Integrators take only what they need: no IMA, no UI or no ad code unless they ask for it.
- Each ecosystem gets the package format its developers expect.
- A reviewer must be able to understand the whole system from one repository.
- Running everything must stay cheap for a one-maintainer project.

## Considered options

1. **One repository; modular packages per ecosystem; apps as thin reference embedders.**
2. **A core repository plus one repository per platform SDK and app** (the VLC mobile pattern).
3. **A core repository plus one repository per ecosystem SDK, with apps beside their SDK.**
4. **One repository and a single all-in-one release bundle** of engine, SDKs and apps.

## Decision

Chosen option: **1**.

**One repository.** `alumbra` contains:

- Umbra and the Penumbra interface;
- every port;
- every SDK and UI renderer;
- the design tokens, the settings schema and the tests;
- the documentation;
- the reference apps.

The existing layout (`umbra/`, `penumbra/`, `antumbra/`, `design/`, `settings/`, `test/`, `docs/`) is the
repository layout.

**Packages.** Each ecosystem gets packages built from the same commit. Modules mirror Media3, so ads and UI are
opt-in:

| Ecosystem | Packages (shape; final coordinates in D-34) | Who uses it |
|-----------|---------------------------------------------|-------------|
| C++ | CMake package (`find_package(Alumbra)`): engine plus a selected port; a vcpkg port | Desktop and embedded integrators: the libVLC / libmpv role |
| Web | npm: core SDK, `<alumbra-player>` web component with UI, IMA provider | Web apps |
| Android / Google TV | Maven: core, Compose UI, IMA | Android apps |
| iOS / iPadOS / tvOS | SwiftPM products: core, SwiftUI UI, IMA, shipped as XCFramework binary targets | Apple apps |
| CLI | `alumbra-cli` command-line player, via a Homebrew tap and release archives | People who want to just play something |

**One version.** All packages share a single SemVer version and are released together from one tag. A release is
one coherent engine, so integrators never have to match versions across ecosystems.

**Apps are reference embedders, not products.** Each app in `antumbra/apps/` uses only its platform's public SDK
and contains no playback, streaming or ad logic. The rule: if an app needs a workaround, the bug is in the SDK. The
apps are:

- `alumbra-cli`, the desktop player;
- the web demo and design playground;
- the Android and Google TV samples;
- the iOS/iPadOS and tvOS samples.

CI builds them on every change. The web demo is deployed to GitHub Pages, and the other builds are attached to
GitHub Releases. App-store publishing is out of scope.

**Satellite repositories only where a tool forces one.** CI generates them from each release; nobody edits them by
hand:

- `homebrew-alumbra`, the Homebrew tap;
- a SwiftPM distribution repository holding only `Package.swift` with binary targets, following Google's IMA pattern.

**Monorepo hygiene.**

- **CI:** workflows are filtered by path, so a web change does not run the Apple jobs.
- **Ownership:** CODEOWNERS is set per directory.
- **Build systems:** each one stays in its own subtree: CMake for C++, Gradle for Android, SwiftPM/Xcode for Apple,
  pnpm for the web.
- **Media:** large test media is fetched by the conformance asset fetcher and never committed.

**Scope.** This ADR settles topology and what is published. D-34 settles how releases are made:

- versioning and changelog automation;
- package coordinates and registry accounts;
- signing and notarisation;
- provenance;
- release channels.

Windows support is a separate question (D-35).

## Consequences

**Positive**
- Atomic changes across the engine, the porting contract, every port and every SDK; no version drift between them.
- The dependency rule and the layer boundaries are enforced by one build graph.
- Integrators get idiomatic packages, and ads and UI stay optional.
- Proving each SDK with a thin reference app keeps the SDKs honest: "easy to embed" is tested, not claimed.
- One place to read, review and star.

**Negative / costs**
- Several build systems coexist in one repository, so contributors need clear per-directory guides.
- CI configuration is more complex (path filters, a per-platform runner matrix); macOS runners are the slowest and
  scarcest.
- Two CI-maintained satellite repositories must be kept in sync by the release pipeline.
- Lockstep versioning means a release can bump a package that did not change.

**Follow-ups**
- D-34 is narrowed to release mechanics, as listed above.
- D-35 decides Windows scope and timing.
- Revisit trigger: split an app into its own repository only if it becomes a product with its own release cycle or
  contributors, as VLC's mobile apps did.

## Pros and cons of the options

### 1. One repository, modular packages, reference apps
- Good: atomic cross-layer changes, a single review surface, idiomatic per-ecosystem packages, optional modules.
- Bad: mixed toolchains and heavier CI in one tree.

### 2. One repository per platform SDK and app
- Good: each repository has one toolchain and a simple CI.
- Bad: every Penumbra or API change becomes coordinated pull requests across repositories, with version skew
  between them. This is the drift that made Flutter merge.

### 3. Core plus per-ecosystem SDK repositories
- Good: the SDKs sit next to their package managers.
- Bad: the bindings are generated from Umbra's API (D-18), so separating them from the engine creates the same skew
  as option 2.

### 4. A single all-in-one bundle
- Good: one artefact.
- Bad: integrators would download apps, ad SDKs and toolchains they do not use. No ecosystem consumes such a
  bundle.

## References

- [Cobalt](https://github.com/youtube/cobalt) · [Media3](https://github.com/androidx/media) ·
  [Video.js](https://github.com/videojs/video.js) · [flutter/engine (archived)](https://github.com/flutter/engine)
- [VLC](https://code.videolan.org/videolan/vlc) · [vlc-android](https://code.videolan.org/videolan/vlc-android) ·
  [vlc-ios](https://code.videolan.org/videolan/vlc-ios) · [mpv](https://github.com/mpv-player/mpv) · [IINA](https://github.com/iina/iina)
- [SwiftPM `binaryTarget`](https://developer.apple.com/documentation/packagedescription/target/binarytarget(name:url:checksum:)) ·
  [IMA iOS SwiftPM distribution](https://github.com/googleads/swift-package-manager-google-interactive-media-ads-ios)
- [Homebrew Taps](https://docs.brew.sh/Taps)
