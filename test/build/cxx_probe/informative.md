# Fixture: unsupported pending and banned features stay informative

## Probed features

| Feature | Probe | Status | Notes |
|---------|-------|--------|-------|
| Fixture that compiles and runs | `fixture_supported` | banned | Stays banned although it compiles. |
| Fixture that does not compile | `fixture_compile_error` | pending | Reported, does not fail. |
| Fixture that exits non-zero | `fixture_run_failure` | pending | Reported, does not fail. |
