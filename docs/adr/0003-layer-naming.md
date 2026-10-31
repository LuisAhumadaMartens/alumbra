# 0003 — Name the three layers Umbra, Penumbra and Antumbra

- **Status:** Accepted
- **Date:** 2026-10-31
- **Deciders:** @LuisAhumadaMartens

## Context

The project is called *Alumbra* (from Spanish *alumbrar*, "to illuminate"). The architecture has three layers
(engine, porting layer, user-facing). Names should be memorable and coherent with the project identity, without
making the code harder to read.

## Decision drivers

- Coherent identity; easy to talk about ("that's a Penumbra concern").
- A reviewer must never need a glossary to understand a module's purpose.

## Considered options

1. **Three layer names only** (Umbra / Penumbra / Antumbra); plain descriptive names inside each layer.
2. Three layer names plus extra themed names for sub-systems (e.g. *Corona* for ads, *Halo* for design).
3. Plain names everywhere (`core`, `port`, `sdk`).

## Decision

Chosen option: **1**, mapping the regions of a shadow from the hidden core outwards:

| Layer | Shadow region | Alumbra meaning |
|-------|---------------|-----------------|
| **Umbra** | the dark core, unseen | Portable C++ engine |
| **Penumbra** | the partial zone where core meets light | Porting layer (our "Starboard") |
| **Antumbra** | beyond the umbra, where a ring of light becomes visible | SDKs, UI, apps, design tokens |

Inside each layer, modules use plain names: `umbra::demux`, `umbra::abr`, `penumbra/android`,
`antumbra/sdk/ts`.

## Consequences

**Positive**
- Distinctive vocabulary limited to three words, defined in the [glossary](../glossary.md).

**Negative / costs**
- Newcomers learn three words. Mitigated by the README table and the glossary.
