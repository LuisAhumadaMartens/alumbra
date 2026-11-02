# Layering checks

The [architecture overview](overview.md#1-layers-and-the-dependency-rule) rests on three dependency rules:
Umbra depends only on the Penumbra interface, ports never depend on each other, and Antumbra depends on Umbra's
public API. Two complementary checks enforce the first two in every build that has tests enabled, so a violation
fails CTest locally and in CI instead of waiting for review.

| Test | What it inspects | Fails when |
|------|------------------|------------|
| `layering_include_test` | `#include`, `#include_next` and `#import` directives in `umbra/`, `penumbra/interface/` and every port under `penumbra/` | Umbra or the interface includes a header outside its allowlist, uses a computed include, or a port includes another port |
| `layering_target_graph_test` | The configured CMake link graph, after generator expressions are evaluated | An `umbra::*` target or `penumbra::interface` reaches a target or library outside its allowlist, directly or transitively, or a port reaches another port |

Both are labelled `layering`. Fixture tests named `layering_include_test.<case>` and
`layering_target_graph_test.<case>` prove that each check passes a clean input and rejects each kind of
violation with the expected message.

## The allowlist

[`tools/layering/allowlist.json`](../../tools/layering/allowlist.json) is the single source of truth for both
checks, and it is owned by maintainers in [CODEOWNERS](../../.github/CODEOWNERS). It has an `includes` section and
a `links` section, each with:

- **scopes**: which files (`root`) or targets (`targets`) a rule applies to, the rule itself, and the entry IDs
  that scope may use;
- **ports**: how ports are recognised (`penumbra/<port>/` directories, `penumbra::<port>` targets) and which
  shared component every port may use (the interface);
- **entries**: exact `names` or `fnmatch`-style `patterns`, each with a mandatory `rationale`. The script rejects
  an entry without one.

Link entries also set `traverse`. Umbra targets and `penumbra::interface` are traversed, so a port reached through
them is still caught. Abseil and `Threads::Threads` are not: they encapsulate their own platform dependencies
(for example CoreFoundation for `absl::time`), and the walk stops at their boundary.

Changing the allowlist is an architectural change: explain the new entry's rationale in the pull request and
link the definition or ADR that justifies it. The generated settings headers (#54) get their entries when that
generator defines its include path. The design-token header (`alumbra/design_tokens/design_tokens.h`, target
`alumbra::design_tokens`) lives in Antumbra and includes only the standard library; renderers use it, Umbra
does not, so it needs no allowlist entry.

## How the checks work

**Includes.** [`tools/layering/check_layering.py`](../../tools/layering/check_layering.py) (Python 3, standard
library only) scans source and header files textually. A quoted include that names an existing file relative to
the including file is classified by its repository path, so `"../penumbra/desktop/desktop.h"` from Umbra is
caught. Every other include is classified by its normalised spelling. Preprocessor conditionals are not
evaluated: an include inside `#if` is still checked, which errs on the side of rejecting.

**Link graph.** [`cmake/AlumbraLayering.cmake`](../../cmake/AlumbraLayering.cmake) runs at the end of
configuration (via `cmake_language(DEFER)`) and writes `<build>/layering/link-graph-<CONFIG>.tsv` with
`file(GENERATE)`. Each target's `LINK_LIBRARIES` and `INTERFACE_LINK_LIBRARIES` are evaluated with
`$<TARGET_GENEX_EVAL>`, so `$<LINK_ONLY>`, configuration conditions and private links of static libraries are
resolved exactly as CMake resolves them. Aliases are mapped back to their targets, and targets are named by their
`EXPORT_NAME` (for example `umbra::base`). The script then walks the graph from every layered target. Imported
targets that are not visible from the top-level directory appear only as names, which the allowlist still
rejects for Umbra.

Both checks fail closed: an unknown header or library is a violation, and so is a run that finds no layered
files or targets.

## Running the checks

```sh
ctest --preset debug -L layering
python3 tools/layering/check_layering.py includes --root .
python3 tools/layering/check_layering.py links --graph build/debug/layering/link-graph-Debug.tsv
```

A violation is reported with its location and, for the link graph, the offending edge and the full path from
the layered target:

```text
umbra/base/base.cc:1: umbra may not include <pthread.h>
umbra: edge umbra::base -> penumbra::desktop is not allowed; path: umbra::base -> penumbra::desktop
ports: edge penumbra::desktop -> penumbra::web links port penumbra::web from port penumbra::desktop; path: penumbra::desktop -> penumbra::web
```

## Fixtures

[`test/build/layering/`](../../test/build/layering/) holds the inputs that keep both checks honest. None of them
is part of the production build.

| Fixture | Expected result |
|---------|-----------------|
| `includes/clean` | Pass: standard, Abseil, Umbra and interface includes in Umbra; platform headers in a port |
| `includes/umbra_platform_header` | `umbra/engine.cc:1: umbra may not include <pthread.h>` |
| `includes/umbra_relative_escape` | A relative include from Umbra that resolves into `penumbra/desktop/` |
| `includes/interface_includes_umbra` | The interface including an Umbra header |
| `includes/port_includes_port` | `penumbra/desktop` including `penumbra/web` |
| `links/clean` | Pass: Umbra through the interface, Abseil and `Threads::Threads`; a port linking platform libraries |
| `links/umbra_links_port` | A static `umbra::engine` privately linking `penumbra::desktop` |
| `links/umbra_reaches_platform_library` | `umbra::engine -> umbra::helper -> SDL3::SDL3` |
| `links/port_links_port` | `penumbra::desktop -> penumbra::web` |

The link fixtures are tiny CMake projects that reuse `cmake/AlumbraLayering.cmake` and the parent build's
toolchain settings; [`run_fixture.cmake`](../../test/build/layering/run_fixture.cmake) configures each one, runs
the check and asserts the outcome and message.
