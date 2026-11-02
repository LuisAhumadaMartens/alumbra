# How we work

Alumbra is planned and built in the open. This page explains how an idea becomes a decision, and a decision
becomes code.

## The flow

```mermaid
flowchart LR
  Q["Open question"] --> D["Definition issue<br/>type:definition"]
  D -->|debate in comments| R["Recommendation converges"]
  R --> A["ADR pull request<br/>docs/adr/NNNN-*.md"]
  A -->|merged| C["Definition closed<br/>(links ADR)"]
  C -->|unblocks| I["Implementation issues<br/>(sub-issues of an epic)"]
  I --> P["Pull requests"]
```

## Definitions

A **definition** is an open design question that is hard to reverse, crosses layers, or is user-visible. Each
definition issue:

- is titled `[D-NN] Topic` and labelled `type:definition` + `status:needs-discussion`;
- states **context**, **options**, **trade-offs**, a **recommendation**, and the **questions to settle**;
- is debated in comments until a decision is reached;
- closes when its ADR is merged, linking to it.

Implementation issues that depend on a definition are **blocked by** it, so the board shows what can start.

## Architecture Decision Records

ADRs live in [`docs/adr/`](adr/) and follow [`0000-template.md`](adr/0000-template.md). They are numbered,
immutable once accepted, and superseded (never edited) when a decision changes. See
[ADR-0001](adr/0001-record-architecture-decisions.md).

## Milestones, epics and issues

| Level | GitHub object | Example |
|-------|---------------|---------|
| Milestone | Milestone `M1 · First Light` | A demoable increment with exit criteria |
| Epic | Issue `type:epic` | `[M1] Desktop playback pipeline` |
| Work item | Sub-issue `type:feature` / `type:task` | `MP4/fMP4 demuxer` |
| Decision | Issue `type:definition` | `[D-05] Demux strategy` |

Every work item has **scope**, **acceptance criteria**, a **definition of done** and **references**.

### Definition of done (all work items)

- Code follows the Google C++ Style Guide and passes clang-format, clang-tidy and CI on all affected platforms.
- Unit tests (and conformance/golden tests where relevant) cover the change; parsers have fuzz targets.
- Public behaviour is documented; settings are added to the schema, never hard-coded.
- No new dependency without a licence entry (ADR-0010).
- Performance budgets are not regressed.

## Labels

| Prefix | Values | Purpose |
|--------|--------|---------|
| `type:` | `definition`, `epic`, `feature`, `task`, `bug`, `docs`, `research`, `chore` | What kind of issue |
| `layer:` | `umbra`, `penumbra`, `antumbra` | Which architectural layer |
| `area:` | `pipeline`, `network`, `hls`, `dash`, `abr`, `demux`, `decode`, `render`, `audio`, `text`, `ads`, `drm`, `ui`, `design`, `settings`, `api`, `build`, `ci`, `perf`, `testing`, `docs` | Functional area |
| `platform:` | `desktop`, `web`, `android`, `google-tv`, `ios`, `tvos` | Platform-specific work |
| `priority:` | `p0`, `p1`, `p2` | Must / should / could within its milestone |
| `status:` | `needs-discussion`, `blocked` | Workflow state not captured by the board |

GitHub's `good first issue`, `help wanted`, `duplicate` and `wontfix` are kept as well. Labels are declared in
[`.github/labels.yml`](../.github/labels.yml) and milestones are generated from the sections of
[`roadmap.md`](roadmap.md); both are applied with
[`scripts/github/sync_repo_metadata.py`](../scripts/github/sync_repo_metadata.py), so the repository never
drifts from the documents.

## The board

The **Alumbra Roadmap** GitHub Project tracks every issue with the fields *Status* (Backlog → Defining → Ready →
In progress → In review → Done), *Milestone*, *Layer*, *Platform*, *Priority* and *Size*, and has views for the
board, the roadmap by milestone, the definitions queue, and work by platform.

## Continuous integration

[`.github/workflows/ci.yml`](../.github/workflows/ci.yml) runs on every pull request (whatever its base branch,
so stacked pull requests are checked too), including edits that retarget the base branch, on every push to
`main` and on demand. Pull request runs share a per-PR concurrency group; push and manual runs have unique
groups so pending runs are not displaced.

[ADR-0021](adr/0021-testing-strategy.md) defines the testing strategy and the merge/release evidence
boundary. The table below describes implemented jobs, not its staged future gates. Missing assets,
calibrated thresholds or real-device/output results remain explicit evidence gaps, not passing checks.

| Job | What it does |
|-----|--------------|
| **Classify changes** | Lists the changed paths and decides whether the C++ matrix and the documentation site checks are needed ([`tools/ci/classify_changes.py`](../tools/ci/classify_changes.py)). |
| **Lint workflows** | Runs actionlint 1.7.12 (checksum-verified download), CI helper regression tests and YAML-based action pin checks ([`tools/ci/check_action_pins.py`](../tools/ci/check_action_pins.py)). |
| **Check C++ formatting** | Runs the pinned `scripts/format --check` over owned sources and prints diffs on failure. |
| **Check C++ lint and feature policy** | Builds `debug` with pinned Linux Clang, runs clang-tidy over every owned translation unit with `WarningsAsErrors: '*'`, and enforces the ADR-0013 feature policy. |
| **Validate settings** | Runs the stdlib-only TOML validator, base-revision compatibility checks, validator unit tests and deterministic parser fuzz smoke; always contributes to `ci-ok`. See [`settings/README.md`](../settings/README.md#validation). |
| **Linux x64 GCC 15**, **Linux x64 Clang 21**, **macOS arm64 Xcode 26.6** | Configure, build and test the `debug` and `release` presets, including the layering, C++ feature probe, installed-consumer and vcpkg manifest tests. |
| **Linux x64 Clang 21 ASan/UBSan**, **Linux x64 Clang 21 TSan**, **macOS arm64 Xcode 26.6 ASan/UBSan** | Run the complete CTest suite under each applicable sanitizer, verify instrumentation in Alumbra compile commands and dependency archives, and preserve the output of deliberately failing opt-in canaries. |
| **Check documentation prose** | Runs `typos` (`en-gb`) over every Markdown file and Vale with the Google style over changed Markdown; Vale's error-level rules block. See [CONTRIBUTING.md](../CONTRIBUTING.md#documentation-site). |
| **Documentation site** | Calls [`.github/workflows/pages.yml`](../.github/workflows/pages.yml): builds the Starlight site with warnings as errors, assembles the Pages artefact, checks internal links and anchors with lychee and, on `main` only, deploys to GitHub Pages. |
| **ci-ok** | Aggregates the others. It fails if any job failed or was cancelled, if the C++ matrix was needed and any build, sanitizer, formatting or lint job did not succeed, or if the site checks were needed and the prose or site job did not succeed. |

The classify job also checks the committed error catalogue and its generator
tests with Python 3.11+, including documentation-only changes. This prevents
catalogue/SDK/reference drift from escaping the C++ path filter.

**Required check.** `ci-ok` is designed to be the always-present aggregate check. When the fail-closed classifier
marks a change as C++-relevant, it requires both the normal build matrix and every sanitizer leg to succeed. It
permits those jobs to skip only when the classifier marks every changed path as documentation. This preserves
ADR-0012's platform/path filtering; an unrelated web or documentation change does not start Apple builds. This
workflow does not configure branch protection or assert that a ruleset already requires the check. The coordinator
merge gate must require it; maintainers may also add it to the `main` ruleset when available.

Linux ASan enables leak detection. Apple's ASan runtime reports that
`detect_leaks` is unsupported, so that option is omitted on macOS.

**Path filter (ADR-0012).** The C++ matrix is skipped only when every changed path is documentation: `docs/**`
except `docs/cpp-features.md` (the C++ probes read it), top-level `*.md`, any `README.md`, `.github/ISSUE_TEMPLATE/**`,
`.github/pull_request_template.md`, `.github/labels.yml`, `.github/CODEOWNERS`, `LICENSE`, `NOTICE`, the site
project `tools/site/**` and the prose-check configuration (`.typos.toml`, `.typos-adr.toml`, `.vale.ini`,
`tools/vale/**`). The
documentation site and prose jobs run when any changed path is Markdown, `docs/**`, `tools/site/**`, prose-check
configuration, a public header under a layer's `include/` directory, `settings/**`, `.github/workflows/**` or
`tools/ci/**` ([ADR-0022](adr/0022-documentation-site-and-api-reference.md)). Any other path, an empty diff or an
event without a comparable base runs everything. Pull requests compare against their
base branch; pushes compare against the previous head. Renames classify both the deleted and added paths;
NUL-delimited Git output preserves literal Unicode, tab and newline filenames.

**Toolchains.** Runners are named explicitly, never `*-latest`. Linux uses `ubuntu-26.04` with `g++-15` and
`clang++-21` (libstdc++ 15) plus `nasm` for the x64 FFmpeg and dav1d builds. macOS uses `macos-26` (arm64) with
`DEVELOPER_DIR=/Applications/Xcode_26.6.app/Contents/Developer`, the ADR-0013 Xcode floor. Each job prints its
compiler, CMake, Ninja and runner image versions and publishes its C++ feature probe matrix in the job summary and
in the `reports-<leg>` artefact, together with the JUnit reports. The matrix describes that runner's toolchain only;
[`docs/cpp-features.md`](cpp-features.md) decides what is allowed.

**vcpkg.** Each job checks out `microsoft/vcpkg` at the manifest's `builtin-baseline` (read from `vcpkg.json`, so
the pin lives in one place), bootstraps it, and uses the `files` binary cache stored with `actions/cache`. The cache
key combines the leg, triplet, a hash of the compiler version (and Xcode version), the baseline and a hash of
`vcpkg.json` and the overlay triplets. Sanitizer triplets are distinct from the ordinary build triplets and from
each other; their cache keys cannot reuse unsanitized or differently instrumented binaries. Partial matches
remain scoped to the same sanitizer leg, and vcpkg reuses or rebuilds packages by ABI hash. The cache is saved
right after the first configure, so a later test failure does not discard a cold build.

**Other rules.** Default token permissions are `contents: read`, checkouts do not persist credentials, superseded
pull request runs are cancelled, and every job has a timeout. Only the Pages workflow's deploy job requests
`pages: write` and `id-token: write`; `ci.yml` grants them to the reusable workflow call because a called
workflow can only narrow its caller's token.

**Pinned actions.** Versions are recorded here because workflow files carry no comments. Update the SHA and this
table together.

The pin checker parses YAML mapping keys with PyYAML 6.0.3 (MIT, CI-only), pinned in
[`tools/ci/requirements.txt`](../tools/ci/requirements.txt). It ignores names, comments and script scalar
text and unrelated `env`/`with` keys. It checks job and step action references (including composite action
steps), quoted/flow mappings, multiline values, anchors and merge keys, and fails on invalid YAML or
non-string action references. Remote actions and reusable workflows require a full commit SHA; Docker
actions require a SHA-256 digest; local actions are allowed. Dependency licensing is recorded in
[`THIRD_PARTY.md`](../THIRD_PARTY.md).

Run the focused checks locally in an isolated environment:

```sh
python3 -m venv .venv-ci
.venv-ci/bin/python -m pip install -r tools/ci/requirements.txt
.venv-ci/bin/python -m unittest discover -s tools/ci -p 'test_*.py'
.venv-ci/bin/python tools/ci/check_action_pins.py .github/workflows
actionlint
```

| Action | Version | Commit |
|--------|---------|--------|
| `actions/checkout` | v7.0.1 | `3d3c42e5aac5ba805825da76410c181273ba90b1` |
| `actions/cache/restore`, `actions/cache/save` | v6.1.0 | `55cc8345863c7cc4c66a329aec7e433d2d1c52a9` |
| `actions/upload-artifact` | v7.0.2 | `cf430e030ddbb5b0abf93d22962f4752f3646cd9` |
| actionlint (release binary) | 1.7.12 | SHA-256 `8aca8db96f1b94770f1b0d72b6dddcb1ebb8123cb3712530b08cc387b349a3d8` |
| `actions/setup-node` | v7.1.0 | `949feb2413d6458794dcd2491c4babbbce0c15c1` |
| `actions/upload-pages-artifact` | v5.0.0 | `fc324d3547104276b827a68afc52ff2a11cc49c9` |
| `actions/deploy-pages` | v5.0.1 | `368f82528645a54fb793d4d04e342629a3f51346` |
| `actions/download-artifact` | v8.0.2 | `9000827ccba6bdab643e8b6fd33ac0654aef8333` |
| Node.js (`tools/site/.nvmrc`) | 26.13.0 | Installed by `actions/setup-node` |
| pnpm (`pnpm-linux-x64` release) | 12.16.1 | SHA-256 `565ff81174a14f6f7bd5d84c1b17e51031a80230d370a1fe24be5d16bd02a825` |
| lychee (`x86_64-unknown-linux-gnu` release) | 0.24.2 | SHA-256 `1f4e0ef7f6554a6ed33dd7ac144fb2e1bbed98598e7af973042fc5cd43951c9a` |
| typos (`x86_64-unknown-linux-musl` release) | 1.51.1 | SHA-256 `93c301fd4120ff076e2cc2a6d6a61cab4a4bdb81899287c0938b452e56d1f454` |
| Vale (`Linux_64-bit` release) | 3.25.1 | SHA-256 `4bf5c5176b682b1da0830f471584d3494f1c2e27ee3029e1ad6f6661d4962182` |
| Vale Google style (`Google.zip`) | 0.7.1 | SHA-256 `4b67fca1f2a88595b2a578ab98bbf3017a23af677b04f93e862843f3ea1a9a0b` |
