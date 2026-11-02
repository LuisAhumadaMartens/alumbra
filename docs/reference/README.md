# Reference

Generated references. Their generators own the pages, so never edit them by hand
([ADR-0022](../adr/0022-documentation-site-and-api-reference.md)).

| Reference | Source | Status |
|-----------|--------|--------|
| [Error catalogue](error-catalogue.md) | `errors/catalogue.toml`, through `tools/errors/generate.py` | Published |
| Settings reference | The settings schema ([ADR-0017](../adr/0017-settings-schema-and-configuration.md)) | Generated into this directory by #55 |
| C++ API reference: Umbra and the C ABI | Installed public headers and the sidecars in `docs/api/cpp/`, through Doxygen | Published at `/api/cpp/` on the site by #63 |
| TypeScript, Kotlin and Swift API references | Each SDK's public declarations and sidecars | Published at `/api/typescript/`, `/api/kotlin/` and `/api/swift/` in M2, M6 and M7 |

The documentation site lists every page in this directory under **Reference**, so generated pages appear in its
navigation without further changes. API references are separate HTML trees that the Pages workflow adds to the
site; see [CONTRIBUTING.md](../../CONTRIBUTING.md#documentation-site).
