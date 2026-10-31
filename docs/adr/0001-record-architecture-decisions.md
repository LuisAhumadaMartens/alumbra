# 0001 — Record architecture decisions, debate them in definition issues

- **Status:** Accepted
- **Date:** 2026-10-31
- **Deciders:** @LuisAhumadaMartens

## Context

Alumbra is designed in the open and is meant to be read: the *why* behind each part matters as much as the code.
Decisions made in chat or in someone's head are lost; decisions made only in code are hard to review.

## Decision drivers

- Every significant decision must be discoverable, reviewable and linkable.
- Debate must happen before implementation, and implementation must be traceable to a decision.
- Low ceremony: one issue, one file.

## Considered options

1. **Definition issues + ADRs** — debate in a GitHub issue, record the outcome as an ADR.
2. **GitHub Discussions + ADRs** — debate in Discussions, record as ADR.
3. **Issues only** — no ADR files.

## Decision

Chosen option: **Definition issues + ADRs.**

- Every open design question is an issue labelled `type:definition` using the *Definition* issue form:
  context, options, trade-offs, recommendation.
- The debate happens in the issue's comments. When it converges, a pull request adds
  `docs/adr/NNNN-title.md` (format: [`0000-template.md`](0000-template.md), based on
  [MADR](https://adr.github.io/madr/)), and the issue is closed with a link to the ADR.
- Implementation issues that depend on a decision are marked **blocked by** the definition issue.
- ADRs are immutable once accepted; a change of mind is a new ADR that supersedes the old one.

## Consequences

**Positive**
- A complete, linkable reasoning trail; implementation can't silently outrun design.
- Issues, the board and ADRs stay in one place (the repository).

**Negative / costs**
- Slightly more process for small decisions — only *significant* decisions (hard to reverse, cross-layer, or
  user-visible) need a definition.

## References

- Michael Nygard, *Documenting Architecture Decisions* (2011).
- [MADR — Markdown Architectural Decision Records](https://adr.github.io/madr/).
