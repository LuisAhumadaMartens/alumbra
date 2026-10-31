# Fixture: allowed features that fail

## Probed features

| Feature | Probe | Status | Notes |
|---------|-------|--------|-------|
| Fixture that compiles and runs | `fixture_supported` | allowed | Passes. |
| Fixture that does not compile | `fixture_compile_error` | allowed | Must fail the check. |
| Fixture that exits non-zero | `fixture_run_failure` | allowed | Must fail the check. |
