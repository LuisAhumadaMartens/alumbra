# Third-party CI tooling

| Dependency | Version | SPDX licence | Use / source |
|------------|---------|--------------|--------------|
| PyYAML | 6.0.3 | MIT | CI-only YAML parsing and regression tests; pinned in [`tools/ci/requirements.txt`](tools/ci/requirements.txt). [Source and licence](https://github.com/yaml/pyyaml/tree/6.0.3). |

PyYAML is installed into an isolated CI environment, not linked into or shipped with Alumbra. Its distribution
includes its MIT licence; preserve that licence if redistributing the tool. MIT is automatically eligible
under [ADR-0014](docs/adr/0014-dependency-and-licensing-policy.md), which makes ADR-0010 precise.

This records the new CI parser dependency, not a complete runtime dependency inventory. The existing runtime
dependency inventory and distribution review remain tracked by #40.
