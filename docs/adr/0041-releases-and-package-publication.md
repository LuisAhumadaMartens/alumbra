# 0041 — Publish nothing before 1.0.0, release by tag and keep package names provisional

- **Status:** Accepted
- **Date:** 2026-11-01
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #34

## Context

D-34 defines versioning, signing, registries and when anything is first published. The maintainer wants the first
public version to be 1.0.0, after the backlog. The issue's plan, `0.N.0` previews per milestone with an npm preview at M4,
contradicts that. The roadmap's M4 line says that when the web package is first published is part of D-34. D-34 blocks
#153 (M4) and #273 (M11).

[ADR-0012](0012-repository-topology-and-packaging.md) sets one repository and one SemVer version for every package,
released from one tag. It names the CLI `alumbra-cli`, where the issue proposes a formula named `alumbra`. It also
allows satellite repositories only where a tool forces one, and names a CI-generated SwiftPM distribution
repository and the Homebrew tap.
[ADR-0014](0014-dependency-and-licensing-policy.md) needs FFmpeg source packaged with any FFmpeg binary before it
is distributed. [ADR-0038](0038-sdk-bindings-through-a-public-c-abi.md) makes the C ABI experimental until 1.0.

## Decision drivers

- No public artefact before 1.0.0, and no squattable placeholder.
- The first release must not be the first packaging test.
- Free to rename the player before 1.0.0.
- Little tooling to maintain for one repository-wide version.
- Provenance on every artefact, and licence obligations met at release.

## Considered options

Pre-1.0 publication:

1. **Nothing**: the first publish is 1.0.0, with CI dry runs only.
2. **Release candidates**: `1.0.0-rc.N` on a `next` tag near the end.
3. **Milestone previews**: `0.N.0` per milestone, with an M4 npm preview.

Release process:

1. **Tag-driven workflow** with GitHub generated notes and labels.
2. **release-please** in manifest mode, with Conventional Commit titles.
3. **Changesets**: per-change files.

## Decision

Chosen options: **nothing public before 1.0.0, and a tag-driven release workflow with generated notes.** The maintainer
may rename the player, so **package and product names are provisional until 1.0.0.**

### Before 1.0.0

- No registry publish and no public release tag. Packaging is exercised earlier by CI dry runs: `npm pack`, a local
  Maven repository build and an XCFramework build, so 1.0.0 is not the first test.
- The names below are plans, not claims. Nothing is reserved, and any of them, including the product name, can change
  before 1.0.0. Documentation and samples must not present them as stable. The first public release fixes them.
- The web package is not published at M4. M4 delivers the build, the dry run and the GitHub Pages demo.

### Versioning

- One version for everything (ADR-0012). After 1.0.0, SemVer covers the SDKs and the public C ABI (ADR-0038). The
  C++ API is source-compatible only.

### Planned coordinates

- **npm:** one package, `alumbra`, with subpath exports (`alumbra/ima`, `alumbra/ui`, `alumbra/settings-panel`).
  The name was unclaimed on the registry on 2026-11-01 (a request returned 404). The WASM binary is fetched on
  demand. The `alumbra-ffmpeg` add-on ([ADR-0036](0036-web-output-selection-and-ffmpeg-wasm-addon.md)) ships beside it.
- **Maven:** groupId `io.github.luisahumadamartens`, with artefacts `alumbra-core`, `alumbra-compose` and
  `alumbra-ima`, through the Central Portal. No snapshots. Central Portal namespace verification for this
  groupId is unverified and is checked at the rehearsal.
- **SwiftPM:** the CI-generated distribution repository that ADR-0012 already names, holding only `Package.swift`
  with `binaryTarget`s that point at release zips. A root manifest in the monorepo is not used, because SwiftPM
  clones the repository it resolves. The repository is created at 1.0.0, not before.
- **Homebrew:** formula `alumbra-cli`, matching ADR-0012, built from source or CI bottles. **vcpkg:** an overlay port
  at 1.0, with a curated-registry submission after.

### Provenance and signing

- `actions/attest-build-provenance` on every artefact, npm provenance, GPG for Maven, SBOMs (D-32) and
  `SHA256SUMS`.
- Apple signing: an Apple Developer Program membership is needed for a notarised macOS CLI archive and signed
  XCFrameworks by 1.0.0. Until then archives are marked unsigned. Homebrew builds are unaffected, though that bottles
  need no notarisation is unverified.
- npm trusted publishing may need the package to exist first (unverified). If so, publish 1.0.0 once manually with
  2FA, then enable trusted publishing and disallow tokens.

### GitHub Release assets at 1.0.0

CLI archives with FFmpeg sources per ADR-0014, C++ SDK archives, XCFrameworks, AARs, and the Android sample APK.

### Release process

- A maintainer pushes a tag, and a workflow builds, attests, publishes and creates the release. Notes come from
  GitHub's generated notes and labels. There are no commit-message rules, and the plain-English pull request titles
  stay.

## Consequences

**Positive**
- Nothing public to maintain or support before the project is ready, and the name stays free to change.
- Packaging is rehearsed throughout, not at the end.
- Little release tooling to maintain.

**Negative / costs**
- Trusted-publisher setup and registry behaviour stay untested until the end.
- Nobody can install a preview, so early outside feedback has to come from the demo site.
- Notes depend on pull request titles and labels being good.

**Follow-ups**
- #153 (M4) packages the TypeScript SDK with CI dry runs only.
- #273 (M11) builds the release workflow and runs the 1.0.0 publish.
- New task: CI packaging dry runs for npm, Maven and XCFramework, from M4 onwards.
- New task: a mark in docs and READMEs that names are provisional until 1.0.0.
- New task: decide on the final names before 1.0.0, then check registry availability again.
- New task: an Apple Developer Program membership, and a rehearsal of the registries (possibly with a private
  registry or an unlisted test namespace) before 1.0.0.
- `docs/roadmap.md` M4 npm line is reworded with this ADR.

## Pros and cons of the options

### Nothing before 1.0.0
- Good, because it is clean, avoids a placeholder and matches the plan.
- Bad, because trusted publishing is untested until the end.

### Release candidates
- Good, because it rehearses the registries.
- Bad, because it puts a public artefact out before 1.0.0.

### Milestone previews
- Good, because it gives early feedback and claims the name.
- Bad, because it contradicts the maintainer's plan and the single-version story.

### Tag-driven workflow
- Good, because there is little tooling and no commit rules.
- Bad, because notes depend on labels.

### release-please
- Good, because changelogs and bumps are automatic.
- Bad, because it changes the current plain-English pull request titles.

### Changesets
- Good, because it suits multi-package npm.
- Bad, because it is a poor fit for one repository-wide version.

## References

- [Semantic Versioning 2.0.0](https://semver.org/)
- [npm provenance](https://docs.npmjs.com/generating-provenance-statements) and
  [trusted publishing](https://docs.npmjs.com/trusted-publishers)
- [GitHub artifact attestations](https://docs.github.com/en/actions/security-guides/using-artifact-attestations-to-establish-provenance-for-builds)
- [Maven Central Portal](https://central.sonatype.org/publish/publish-portal-guide/)
- [SwiftPM `binaryTarget`](https://developer.apple.com/documentation/packagedescription/target/binarytarget(name:url:checksum:))
- [GitHub generated release notes](https://docs.github.com/en/repositories/releasing-projects-on-github/automatically-generated-release-notes)
- [Apple: notarising macOS software](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)
- [ADR-0012](0012-repository-topology-and-packaging.md), [ADR-0014](0014-dependency-and-licensing-policy.md),
  [ADR-0036](0036-web-output-selection-and-ffmpeg-wasm-addon.md), [ADR-0038](0038-sdk-bindings-through-a-public-c-abi.md)
