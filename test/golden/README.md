# Golden tests

Reserved for deterministic reference-output comparisons as media modules arrive.

The desktop video output's golden-frame tests are in `test/penumbra_desktop/` (target
`penumbra_desktop_golden_test`, CTest label `golden`). Their references are rendered by an independent CPU
reference in the test, not committed images; see [desktop video output](../../docs/architecture/desktop-video.md#tests).
