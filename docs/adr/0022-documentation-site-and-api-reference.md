# 0022 — Build the documentation site with Starlight and keep API documentation in sidecar files

- **Status:** Accepted
- **Date:** 2026-11-01
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #33

## Context

Alumbra's documentation is plain Markdown in `docs/`: vision, architecture, roadmap, ADRs and research notes.
GitHub renders it, including Mermaid diagrams, and it stays the single source. A site has to add what GitHub
cannot:

- navigation and search;
- generated API references for C++ (Umbra and the C ABI), TypeScript, Kotlin and Swift;
- the settings reference generated from the [ADR-0017](0017-settings-schema-and-configuration.md) schema (#55);
- live embeds of `<alumbra-player>` and the design playground ([ADR-0019](0019-design-tokens.md)).

A repository has one GitHub Pages site, so the docs, the web demo and the playground are built into one Pages
artefact by one workflow ([ADR-0012](0012-repository-topology-and-packaging.md)).

Alumbra-owned code has no comments. API documentation therefore cannot come from documentation comments,
docstrings or snippet markers. The tools must read it from elsewhere and still enforce that every public symbol is
documented. Doxygen can document a symbol from a separate file with structural commands (`\fn`, `\class`,
`\struct`, …) and DocC has documentation extension files. TypeDoc and Dokka read only comments in the sources they
are given.

## Decision drivers

- `docs/` stays the single source and reads well on GitHub.
- The site's distinguishing content is interactive: player demos, the design playground and settings examples.
- API documentation without source comments, with coverage enforced by the build.
- One GitHub Pages deployment together with the web demo and the playground.
- No external service or account in the critical path; low upkeep for a single maintainer.

## Considered options

Site generator:

1. **A — MkDocs with Material** (Zensical later).
2. **B — Docusaurus.**
3. **C — Astro Starlight.**
4. **D — Sphinx with Breathe or Exhale.**

API documentation source:

1. **Sidecar files**, outside the sources, read by each language's native generator.
2. **Documentation comments on public declarations only**, as an exception to the no-comments rule.
3. **Hand-written reference pages**, without a generator.

## Decision

Chosen options: **C, Astro Starlight**, and **sidecar files**. Starlight embeds web components without a framework
layer, ships Pagefind search, and runs on the Node toolchain the web SDK already needs. Sidecar files keep the
no-comments rule absolute: Doxygen and DocC read them natively, and the build fails on missing or stale entries.

### Site

- The Starlight project lives in `tools/site/` with a pinned Node version and a committed lockfile.
- Content is loaded from `docs/` in place through an Astro content collection; nothing is copied or symlinked.
  The collection excludes generator inputs under `docs/api/`, so sidecar files never become pages. A remark plugin rewrites relative `.md` links and `github.com/…/blob/main/docs/…` links to site routes. Links to
  anything outside `docs/` stay GitHub links.
- ADRs and research notes are site sections. Mermaid diagrams are rendered to SVG at build time.
- Generated pages, such as the settings reference (#55) and the error catalogue, are produced by their generators
  and never edited by hand.

### Hosting and layout

- The site is served at `https://luisahumadamartens.github.io/alumbra/`. The base path is configured in one place,
  so a custom domain later is a configuration change plus a DNS record; GitHub redirects the old URL.
- One workflow assembles one Pages artefact:
  - `/` — documentation;
  - `/demo/` — the web demo;
  - `/playground/` — the design playground;
  - `/api/cpp/`, `/api/typescript/`, `/api/kotlin/`, `/api/swift/` — API references.
- Pushes to `main` deploy. Pull requests build the site without deploying.

### API references

- Each language uses its native generator and keeps its own HTML subtree. The subtrees are themed lightly, not
  unified into one theme.
- Reference covers only public surfaces: installed headers (the C ABI and the C++ API) and the SDKs' public APIs.
  Internal symbols are excluded by choosing the generator inputs, not by markers in the sources.
- Documentation is written in sidecar Markdown files, never in source comments:
  - **C++ (#63).** Doxygen with doxygen-awesome-css. The inputs are the installed public headers and the sidecars
    in `docs/api/cpp/`, which document symbols with structural commands. `EXTRACT_ALL` is off,
    `WARN_IF_UNDOCUMENTED` is on and warnings fail the build, so an undocumented public symbol, or a sidecar entry
    that matches no declaration, fails CI.
  - **Swift (M7).** DocC documentation extension files in the package's documentation catalogue, with DocC's
    coverage report as the gate.
  - **TypeScript (M2) and Kotlin (M6).** A build step injects the sidecar text into a generated copy of the public
    declarations, never committed, and runs TypeDoc (`validation.notDocumented` with warnings as errors) or Dokka
    (`reportUndocumented` with `failOnWarning`) on that copy. Before generating the copy, the injection step fails
    on a sidecar entry that matches no declaration or duplicates another, as Doxygen does. The injection tools are
    built with those SDKs.
- Code examples come from whole files or syntax-aware selections of files that CI compiles, never from snippet
  markers in comments.
- MrDocs is re-evaluated if it gains external documentation input; today it reads comments only.

### Search

Pagefind, built into Starlight. The API subtrees keep their generators' own search.

### Versioning

The site is unversioned and publishes `main` until Alumbra's first version, 1.0.0, which is cut after the milestone
backlog is complete. From 1.0.0 the site keeps one snapshot per minor release, using the `starlight-versions`
plugin; the policy is revisited at 1.0.0.

### Quality gates

On every pull request that touches documentation, the site, public headers, public SDK declarations or the
settings schema:

- the site builds, with warnings as errors;
- lychee checks the built site; internal links are blocking, while external links run with retries and an
  exclusion list in a scheduled, non-blocking job;
- the C++ reference coverage check passes;
- each SDK's reference coverage check (TypeDoc, Dokka or DocC) passes; each check becomes blocking in the
  milestone that adds its SDK (M2, M6 and M7);
- `typos`, with the `en-gb` locale, checks Markdown and prose for misspellings and American spellings;
- Vale checks changed Markdown with the Google developer documentation style and a project vocabulary. Rules at
  error level block; suggestions and warnings are reported without blocking, and rules are promoted as the docs
  settle.

## Consequences

**Positive**

- `docs/` stays the only source and keeps rendering on GitHub; the site adds navigation, search and embeds.
- Player and playground embeds are native, and nothing depends on an external service.
- API coverage is enforced without breaking the no-comments rule.

**Negative / costs**

- CI gains a Node toolchain for the site and Doxygen for the C++ reference.
- Versioning and Mermaid rely on Starlight community plugins.
- Sidecar documentation lives apart from declarations: a rename needs two edits, though the build catches misses.
- TypeScript and Kotlin references need small injection tools.
- Vale rules need tuning as the docs grow.

**Follow-ups**

- #62: the Starlight site, the Pages workflow, lychee, `typos` and Vale.
- #63: the Doxygen sidecars in `docs/api/cpp/` and the coverage gate.
- #55: the settings reference pages and their machine-readable index for the site.
- Whether shipped headers, `.d.ts` files and source archives carry injected documentation for IDE hover is
  decided with packaging (D-34, #34).
- Versioned snapshots start at 1.0.0.

## Pros and cons of the options

### A. MkDocs with Material

- Good, because it has the lowest friction for Markdown, with excellent navigation and search.
- Bad, because Material is in maintenance mode while its authors build Zensical, so a migration is coming.
- Bad, because the player and playground embeds need raw HTML and JavaScript.

### B. Docusaurus

- Good, because it is mature, with built-in versioning, i18n, Mermaid and Algolia integration.
- Bad, because it adds a heavier React toolchain, and versioned docs are full copies of the docs tree.

### C. Astro Starlight

- Good, because web components and the playground embed natively as islands, and Pagefind is built in.
- Good, because Astro's content loader reads `docs/` in place.
- Bad, because it is younger, and versioning, TypeDoc integration and Mermaid come from community plugins.

### D. Sphinx with Breathe or Exhale

- Good, because it has the most tightly integrated C++ reference.
- Bad, because it is the weakest for interactive embeds and needs reStructuredText or MyST.
- Bad, because the TypeScript, Kotlin and Swift references stay separate anyway.

### Sidecar files

- Good, because the sources stay comment-free and coverage is enforced by the generators.
- Bad, because documentation is separated from declarations, and TypeScript and Kotlin need injection tools.

### Documentation comments on public declarations

- Good, because every generator and IDE supports them without extra tooling.
- Bad, because they break the no-comments rule, and review must keep them limited to public declarations.

### Hand-written reference pages

- Good, because they are the simplest to start.
- Bad, because nothing checks coverage and the pages drift from the code.

## References

- [Starlight](https://starlight.astro.build/)
- [Astro content collections](https://docs.astro.build/en/guides/content-collections/)
- [starlight-versions](https://github.com/HiDeoo/starlight-versions)
- [Pagefind](https://pagefind.app/)
- [Doxygen: documentation at other places](https://www.doxygen.nl/manual/docblocks.html#structuralcommands)
- [Doxygen configuration: WARN_IF_UNDOCUMENTED, WARN_AS_ERROR](https://www.doxygen.nl/manual/config.html)
- [doxygen-awesome-css](https://jothepro.github.io/doxygen-awesome-css/)
- [DocC: adding supplemental content to a documentation catalog](https://www.swift.org/documentation/docc/adding-supplemental-content-to-a-documentation-catalog)
- [TypeDoc validation options](https://typedoc.org/documents/Options.Validation.html)
- [Dokka Gradle configuration](https://kotlinlang.org/docs/dokka-gradle.html)
- [MrDocs](https://www.mrdocs.com/)
- [GitHub Pages: custom workflows](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages)
- [lychee](https://lychee.cli.rs/)
- [typos](https://github.com/crate-ci/typos)
- [Vale](https://vale.sh/) and the [Google style package](https://github.com/errata-ai/Google)
