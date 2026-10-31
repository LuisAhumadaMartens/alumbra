# Antumbra — what users touch

- `sdk/` — bindings: TypeScript, Kotlin, Swift and C++.
- `ui/` — thin renderers of the headless UI state machine: Web Components, Compose, SwiftUI, desktop.
- `apps/` — `alumbra-cli`, the web demo and design playground, Android/Google TV and iOS/tvOS sample apps.

Renderers own pixels and input only; behaviour lives in Umbra's UI state machine and look in the
[design tokens](../design/). See [ADR-0007](../docs/adr/0007-ui-architecture.md).
