# Antumbra — what users touch

- `sdk/` — bindings: TypeScript, Kotlin, Swift and C++.
- `ui/` — thin renderers of the headless UI state machine: Web Components, Compose, SwiftUI, desktop.
- `apps/` — reference embedders: `alumbra-cli`, the web demo and design playground, Android/Google TV and
  iOS/tvOS samples. They use only the public SDKs and contain no player logic; if an app needs a workaround, the
  bug is in the SDK.
- `design_tokens/` — the generated C++ design-token header, exported as the CMake target `alumbra::design_tokens`.
- `generated/` — committed generated sources: the error codes and the CSS, TypeScript, Swift and Kotlin design
  tokens ([generated outputs](../design/README.md#generated-outputs)). Edit their sources, not these files.

Each ecosystem gets its own modular packages (CMake/vcpkg, npm as `alumbra`, Maven, SwiftPM, Homebrew), all
released together under one version, with ads and UI as optional modules. See
[ADR-0012](../docs/adr/0012-repository-topology-and-packaging.md).

Renderers own pixels and input only; behaviour lives in Umbra's UI state machine and look in the
[design tokens](../design/). See [ADR-0007](../docs/adr/0007-ui-architecture.md).
