# Third-party CI tooling

| Dependency | Version | SPDX licence | Use / source |
|------------|---------|--------------|--------------|
| PyYAML | 6.0.3 | MIT | CI-only YAML parsing and regression tests; pinned in [`tools/ci/requirements.txt`](tools/ci/requirements.txt). [Source and licence](https://github.com/yaml/pyyaml/tree/6.0.3). |

PyYAML is installed into an isolated CI environment, not linked into or shipped with Alumbra. Its distribution
includes its MIT licence; preserve that licence if redistributing the tool. MIT is automatically eligible
under [ADR-0014](docs/adr/0014-dependency-and-licensing-policy.md), which makes ADR-0010 precise.

This records the new CI parser dependency, not a complete runtime dependency inventory. The existing runtime
dependency inventory and distribution review remain tracked by #40.

## Documentation site tooling

Build-time tools only: nothing below is linked into, shipped with or loaded by an Alumbra artefact. The site
project in [`tools/site/`](tools/site/) pins its direct dependencies in `package.json` and its full closure,
including integrity hashes, in `pnpm-lock.yaml`.

| Tool | Version | SPDX licence | Use |
|------|---------|--------------|-----|
| Astro | 7.4.4 | MIT | Static site generator |
| Starlight (`@astrojs/starlight`) | 0.42.8 | MIT | Documentation theme, navigation and Pagefind search |
| `@astrojs/markdown-remark` | 7.3.5 | MIT | Remark and rehype processing |
| rehype-mermaid | 3.0.0 | MIT | Renders Mermaid diagrams to SVG at build time |
| Playwright | 1.64.1 | Apache-2.0 | Headless Chromium for rehype-mermaid; the browser is downloaded at build time |
| pnpm | 12.16.1 | MIT | Package manager |
| lychee | 0.24.2 | Apache-2.0 OR MIT | Link checker |
| typos | 1.51.1 | Apache-2.0 OR MIT | Spell checker |
| Vale | 3.25.1 | MIT | Prose linter |
| Vale Google style | 0.7.1 | MIT | Vale rule package |

The npm closure (455 packages) is MIT, ISC, BSD-2-Clause, BSD-3-Clause, Apache-2.0, BlueOak-1.0.0, CC0-1.0,
Unlicense and Python-2.0, plus MPL-2.0 (`lightningcss`, used only by Vite while building), `(MPL-2.0 OR
Apache-2.0)` (`dompurify`) and `(CC-BY-4.0 AND OFL-1.1 AND MIT)` (`@fortawesome/fontawesome-free`, whose icon font
Mermaid's headless renderer loads). Sharp and its LGPL-3.0 libvips binaries are removed with a pnpm override because
the site uses no image optimisation. Because none of this is distributed, ADR-0014's distribution conditions do not
apply; the closure's licences are recorded here rather than gated by the vcpkg inventory check.
