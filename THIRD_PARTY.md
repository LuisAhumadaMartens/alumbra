# Third-party CI tooling

| Dependency | Version | SPDX licence | Use / source |
|------------|---------|--------------|--------------|
| PyYAML | 6.0.3 | MIT | CI-only YAML parsing and regression tests; pinned in [`tools/ci/requirements.txt`](tools/ci/requirements.txt). [Source and licence](https://github.com/yaml/pyyaml/tree/6.0.3). |
| TypeScript | 7.0.2 | Apache-2.0 | Strict type-check of the generated design-token TypeScript; locked in [`tools/design/package-lock.json`](tools/design/package-lock.json). [Source and licence](https://github.com/microsoft/TypeScript). |
| stylelint | 17.16.0 | MIT | Lints the generated design-token CSS; locked in [`tools/design/package-lock.json`](tools/design/package-lock.json). [Source and licence](https://github.com/stylelint/stylelint). |
| stylelint-config-standard | 40.0.0 | MIT | The stylelint rule set, with the overrides in [`tools/design/stylelint.config.json`](tools/design/stylelint.config.json). [Source and licence](https://github.com/stylelint/stylelint-config-standard). |
| Kotlin compiler | 2.4.21 | Apache-2.0 | Compiles the generated design-token Kotlin; the release archive is pinned by SHA-256 in [`.github/workflows/ci.yml`](.github/workflows/ci.yml). [Source and licence](https://github.com/JetBrains/kotlin/tree/v2.4.21). |

PyYAML is installed into an isolated CI environment, not linked into or shipped with Alumbra. Its distribution
includes its MIT licence; preserve that licence if redistributing the tool. MIT is automatically eligible
under [ADR-0014](docs/adr/0014-dependency-and-licensing-policy.md), which makes ADR-0010 precise.

The design-token tools check generated files in CI and on developer machines; none of them, and none of their
dependencies, is linked into or shipped with Alumbra, and the generated outputs depend on none of them.
`npm ci` installs the exact versions and integrity hashes in the lockfile. Its 139 transitive packages are
licensed MIT (100), Apache-2.0 (21, mostly TypeScript's per-platform compiler packages), ISC (9), MIT-0 (3),
BSD-3-Clause (3), Python-2.0 (`argparse`) and CC0-1.0 (`mdn-data`); `svg-tags` declares MIT in its legacy
`licenses` field. MIT-0, Python-2.0 and CC0-1.0 are permissive but outside ADR-0014's automatic list, which
governs dependencies of Alumbra's artefacts; they are recorded here for that reason. CI also uses Node.js
24.22.2 and the Temurin 21.0.13+9 JDK through the pinned `actions/setup-node` and `actions/setup-java` actions,
and Swift from the pinned Xcode on macOS, as runtimes for these checks only.

This records the CI tooling dependencies, not a complete runtime dependency inventory. The existing runtime
dependency inventory and distribution review remain tracked by #40.
