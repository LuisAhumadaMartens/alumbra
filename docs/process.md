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

## The board

The **Alumbra Roadmap** GitHub Project tracks every issue with the fields *Status* (Backlog → Defining → Ready →
In progress → In review → Done), *Milestone*, *Layer*, *Platform*, *Priority* and *Size*, and has views for the
board, the roadmap by milestone, the definitions queue, and work by platform.
