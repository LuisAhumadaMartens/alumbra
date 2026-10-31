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
| [0006](0006-platform-order.md) | Platform order: Desktop → Web → Android/Google TV → Apple | Accepted |
| [0007](0007-ui-architecture.md) | UI: design tokens + headless UI state machine + native renderers | Accepted |
| [0008](0008-ads-architecture.md) | Ads: vendor-neutral AdManager, IMA per platform, built-in VAST/VMAP | Accepted |
| [0009](0009-toolchain.md) | Toolchain: C++20, CMake presets, vcpkg, Google style, GoogleTest, Abseil (language standard superseded by [0013](0013-cpp-language-standard.md)) | Accepted |
| [0010](0010-license-and-visibility.md) | Public, Apache-2.0, LGPL/permissive-only dependencies | Accepted |
| [0011](0011-sources-open-network-stream.md) | Accept any typed source URL (VLC "Open Network Stream") | Accepted |
| [0012](0012-repository-topology-and-packaging.md) | One repository, modular packages per ecosystem, apps as thin reference embedders | Accepted |
| [0013](0013-cpp-language-standard.md) | Build with C++23, keep public headers at C++20, allow library features one by one | Accepted |
| [0014](0014-dependency-and-licensing-policy.md) | Permissive dependencies, platform-constrained weak copyleft and reproducible licence review | Accepted |
| [0015](0015-threading-and-task-runners.md) | Order engine work on sequenced task runners | Accepted |

New ADRs start from [0000-template.md](0000-template.md).
