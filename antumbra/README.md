# Antumbra — what users touch

- `sdk/` — bindings: TypeScript, Kotlin, Swift and C++.
- `ui/` — thin renderers of the headless UI state machine: Web Components, Compose, SwiftUI, desktop.
- `apps/` — reference embedders: `alumbra-cli`, the web demo and design playground, Android/Google TV and
  iOS/tvOS samples. They use only the public SDKs and contain no player logic; if an app needs a workaround, the
  bug is in the SDK.

Each ecosystem gets its own modular packages (CMake/vcpkg, npm, Maven, SwiftPM, Homebrew), all released together
under one version, with ads and UI as optional modules. See
[ADR-0012](../docs/adr/0012-repository-topology-and-packaging.md).

Renderers own pixels and input only; behaviour lives in Umbra's UI state machine and look in the
[design tokens](../design/). See [ADR-0007](../docs/adr/0007-ui-architecture.md).
