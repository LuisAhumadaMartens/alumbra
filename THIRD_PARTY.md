# Third-party CI tooling

| Dependency | Version | SPDX licence | Use / source |
|------------|---------|--------------|--------------|
| PyYAML | 6.0.3 | MIT | CI-only YAML parsing and regression tests; pinned in [`tools/ci/requirements.txt`](tools/ci/requirements.txt). [Source and licence](https://github.com/yaml/pyyaml/tree/6.0.3). |
| Doxygen | 1.18.0 | GPL-2.0 | CI and developer tool that generates the C++ API reference; release binaries pinned by SHA-256 in [`tools/cpp-reference/doxygen.py`](tools/cpp-reference/doxygen.py). [Source and licence](https://github.com/doxygen/doxygen/tree/Release_1_18_0). |
| doxygen-awesome-css | 2.5.0 (commit `46483f1e5a70ffb9ecd3b82d0a1cd1b24edf13da`) | MIT | Theme of the published C++ API reference. `doxygen-awesome.css` (SHA-256 `ea37516ec7a7a7780719b9d9a285fe3268a8d5411523d8b93af98d77e67cd1a1`) and `LICENSE` (SHA-256 `e3da754c3f657cc78594fa2e8a3283665f78c743df2485fa9e498a8973051191`) are vendored unmodified in [`tools/cpp-reference/third_party/doxygen-awesome-css/`](tools/cpp-reference/third_party/doxygen-awesome-css/). [Source and licence](https://github.com/jothepro/doxygen-awesome-css/tree/v2.5.0). |

PyYAML is installed into an isolated CI environment, not linked into or shipped with Alumbra. Its distribution
includes its MIT licence; preserve that licence if redistributing the tool. MIT is automatically eligible
under [ADR-0014](docs/adr/0014-dependency-and-licensing-policy.md), which makes ADR-0010 precise.

Doxygen runs only on CI runners and developer machines, as [ADR-0022](docs/adr/0022-documentation-site-and-api-reference.md)
decides; it is never linked into or shipped with Alumbra. Its licence states that documents it produces are
derivative works of their input and are not affected by that licence. The JavaScript files that Doxygen copies
into the generated HTML carry their own MIT licence notices, which the published reference keeps intact.

doxygen-awesome-css is published with the C++ API reference on the documentation site. The stylesheet keeps its
SPDX identifier and copyright notice, and the build publishes its `LICENSE` beside the reference as
`LICENSE-doxygen-awesome-css.txt`. Keep both when updating the theme, and update the version, commit and
checksums here.

This records the CI and documentation tooling dependencies, not a complete runtime dependency inventory. The
existing runtime dependency inventory and distribution review remain tracked by #40.
