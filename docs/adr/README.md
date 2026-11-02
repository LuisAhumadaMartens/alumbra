# Architecture Decision Records

Settled decisions. Open questions live in
[definition issues](https://github.com/LuisAhumadaMartens/alumbra/issues?q=label%3Atype%3Adefinition); see
[how we work](../process.md).

| # | Decision | Status |
|---|----------|--------|
| [0001](0001-record-architecture-decisions.md) | Record architecture decisions, debate them in definition issues | Accepted |
| [0002](0002-portable-engine-porting-layer-html5-api.md) | One portable C++ engine behind a porting layer, with an HTMLMediaElement-shaped API | Accepted |
| [0003](0003-layer-naming.md) | Name the layers Umbra, Penumbra and Antumbra | Accepted |
| [0004](0004-web-output-hybrid-mse-webcodecs.md) | Web output: MSE by default, WebCodecs + WebGL fallback | Accepted |
| [0005](0005-drm-scope.md) | DRM in stages: ClearKey/CENC, then platform DRM | Accepted |
| [0006](0006-platform-order.md) | Platform order: Desktop → Web → Android/Google TV → Apple (platform scope superseded in part by [0031](0031-windows-support-scope-and-toolchain.md)) | Accepted |
| [0007](0007-ui-architecture.md) | UI: design tokens + headless UI state machine + native renderers | Accepted |
| [0008](0008-ads-architecture.md) | Ads: vendor-neutral AdManager, IMA per platform, built-in VAST/VMAP | Accepted |
| [0009](0009-toolchain.md) | Toolchain: C++20, CMake presets, vcpkg, Google style, GoogleTest, Abseil (language standard superseded by [0013](0013-cpp-language-standard.md)) | Accepted |
| [0010](0010-license-and-visibility.md) | Public, Apache-2.0, LGPL/permissive-only dependencies | Accepted |
| [0011](0011-sources-open-network-stream.md) | Accept any typed source URL (VLC "Open Network Stream") | Accepted |
| [0012](0012-repository-topology-and-packaging.md) | One repository, modular packages per ecosystem, apps as thin reference embedders | Accepted |
| [0013](0013-cpp-language-standard.md) | Build with C++23, keep public headers at C++20, allow library features one by one | Accepted |
| [0014](0014-dependency-and-licensing-policy.md) | Permissive dependencies, platform-constrained weak copyleft and reproducible licence review (web restriction superseded in part by [0036](0036-web-output-selection-and-ffmpeg-wasm-addon.md)) | Accepted |
| [0015](0015-threading-and-task-runners.md) | Order engine work on sequenced task runners | Accepted |
| [0016](0016-penumbra-port-contract.md) | Define the port contract as a push-model player with a shared reference implementation | Accepted |
| [0017](0017-settings-schema-and-configuration.md) | Define settings in a TOML schema with layered, validated, generated access | Accepted |
| [0018](0018-error-model-and-recovery.md) | Carry errors as `absl::Status` with a public catalogue and one recovery ladder | Accepted; URL redaction refined by [0020](0020-observability-logging-tracing-qoe-metrics.md) |
| [0019](0019-design-tokens.md) | Generate design tokens from pure DTCG sources with wide-gamut and HDR support | Accepted |
| [0020](0020-observability-logging-tracing-qoe-metrics.md) | Build a platform-neutral observability facade for playback | Accepted |
| [0021](0021-testing-strategy.md) | Use deterministic in-process testing with mandatory real-port conformance | Accepted on merge |
| [0022](0022-documentation-site-and-api-reference.md) | Build the documentation site with Starlight and keep API documentation in sidecar files | Accepted |
| [0023](0023-public-api-surface-and-media-error.md) | Expose a strict HTMLMediaElement subset, checked against browsers, with attachable modules | Accepted |
| [0024](0024-media-clock-and-av-sync.md) | Drive media time from a smoothed audio clock, pick frames by cadence and offer display resample | Accepted |
| [0025](0025-demuxing-strategy.md) | Parse core containers in Umbra and use libavformat only as a desktop fallback | Accepted |
| [0026](0026-network-stack-and-http-transport.md) | Fetch through each platform's HTTP stack behind one streaming transport interface | Accepted |
| [0027](0027-protocol-scope-and-url-resolution.md) | Resolve URLs through a scheme registry and build live protocols on libsrt and shared native code | Accepted |
| [0028](0028-headless-ui-state-machine.md) | Build the headless UI as typed region reducers that publish frame-coalesced snapshots | Accepted |
| [0029](0029-decoder-selection-and-fallback.md) | Select decoders in the reference player through an ordered fallback chain | Accepted |
| [0030](0030-desktop-graphics-audio-and-ui-renderer.md) | Build the desktop port on SDL3 and SDL_GPU, with Alumbra's own UI renderer and a replaceable presenter | Accepted |
| [0031](0031-windows-support-scope-and-toolchain.md) | Build and test the engine on Windows 11 with clang-cl from M1, and defer playback until after M7 | Accepted |
| [0032](0032-unified-presentation-model.md) | Parse HLS and DASH into one presentation model with explicit periods | Accepted |
| [0033](0033-abr-algorithm-and-bandwidth-estimation.md) | Ship a hybrid ABR policy over a dual-EWMA estimator, with caps applied first | Accepted |
| [0034](0034-buffer-model-and-memory-budgets.md) | Set buffer goals in time, clamp them by byte budgets and keep pull-based pacing in the port | Accepted |
| [0035](0035-text-cue-model-styling-and-ass-renderer.md) | Resolve text layout in Umbra, merge user styles field by field and render ASS with Alumbra's own subset renderer | Accepted |
| [0036](0036-web-output-selection-and-ffmpeg-wasm-addon.md) | Select the web output path by probe and add an on-demand FFmpeg WASM add-on for full compatibility | Accepted |
| [0037](0037-wasm-threading-and-cross-origin-isolation.md) | Build the web engine twice, threaded when the page is cross-origin isolated and single-threaded otherwise | Accepted |
| [0038](0038-sdk-bindings-through-a-public-c-abi.md) | Bind every SDK through one public C ABI that stays experimental until 1.0 | Accepted |
| [0039](0039-compatibility-matrix-and-support-tiers.md) | Support platforms in evidence-based tiers, with Apple 17/14 and Android 9 floors | Accepted |
| [0040](0040-fullscreen-and-picture-in-picture.md) | Use element or pseudo-fullscreen, and hand off to picture-in-picture by suspending and restoring the UI | Accepted |
| [0041](0041-releases-and-package-publication.md) | Publish nothing before 1.0.0, release by tag and keep package names provisional | Accepted |

New ADRs start from [0000-template.md](0000-template.md).
