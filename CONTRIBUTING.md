# Contributing to Alumbra

Thanks for your interest! Alumbra is designed in the open; contributions to the *design* are as welcome as
contributions to the code.

## Before you start

1. Read the [vision](docs/vision.md), the [architecture overview](docs/architecture/overview.md) and
   [how we work](docs/process.md).
2. Check the [board](https://github.com/users/LuisAhumadaMartens/projects) and the
   [open definitions](https://github.com/LuisAhumadaMartens/alumbra/issues?q=is%3Aopen+label%3Atype%3Adefinition).
   Work that is **blocked by** an open definition should not start until the definition is settled.

## Proposing a design change

Open a **Definition** issue (issue form) with context, options, trade-offs and your recommendation. Significant
changes are accepted only through an [ADR](docs/adr/).

## Reporting a bug

Use the **Bug** issue form. Include the source URL (or a minimal reproducible stream), platform, version, settings
diff and logs. Never post DRM licence URLs, tokens or ad unit credentials.

## Making a change

- One logical change per pull request; link the issue it resolves.
- Follow the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html); run `clang-format` and
  `clang-tidy`.
- Respect the layer rule: **Umbra must not include platform headers**; ports never depend on each other.
- New options belong in the settings schema, never as hard-coded constants.
- New dependencies require a licence entry and must comply with [ADR-0010](docs/adr/0010-license-and-visibility.md).
- Add tests: unit tests always; conformance/golden tests for media behaviour; fuzz targets for parsers.
- Commit messages: imperative mood, ≤ 72-character subject, body explains *why*.

## Code of conduct

Participation is governed by the [Code of Conduct](CODE_OF_CONDUCT.md).
