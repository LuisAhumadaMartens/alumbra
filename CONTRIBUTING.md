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

- Name development branches `<kind>/issue-<number>`, where `kind` is the issue's
  `type:` label (for example, `task/issue-37` or `feature/issue-20`).
- One logical change per pull request; link the issue it resolves.
- Follow the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html); run `clang-format` and
  `clang-tidy`.
- Do not add comments to Alumbra-owned source or build code; put documentation in Markdown. Preserve mandatory
  upstream legal notices and non-comment file directives.
- Respect the layer rule: **Umbra must not include platform headers**; ports never depend on each other. The
  [layering checks](docs/architecture/layering.md) enforce it; changing their allowlist needs a rationale.
- New options belong in the settings schema, never as hard-coded constants.
- New dependencies require a licence entry and must comply with [ADR-0010](docs/adr/0010-license-and-visibility.md).
- Add tests: unit tests always; conformance/golden tests for media behaviour; fuzz targets for parsers
  ([fuzz targets](test/fuzz/README.md)).
- Commit messages: imperative mood, ≤ 72-character subject, body explains *why*.

## Building the desktop skeleton

Prerequisites for macOS arm64 and Ubuntu LTS:

- A C++23 toolchain at or above the [ADR-0013](docs/adr/0013-cpp-language-standard.md)
  floors, each a compiler paired with its standard library: Xcode 26.6 or newer
  (or the matching Command Line Tools) on macOS; GCC 15 with libstdc++ 15, or
  Clang 21 with libstdc++ 15 or libc++ 21, on Linux.
- CMake **3.28 or newer** (preset schema version 8) and Ninja.
- Git and a bootstrapped vcpkg checkout. Set `VCPKG_ROOT` to its absolute path;
  presets load `$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake`.
- Python 3 (standard library only) for the layering tests; configuration fails
  without it unless `ALUMBRA_BUILD_TESTS=OFF`.

For example, on Ubuntu 26.04 LTS install `build-essential`, `cmake`,
`ninja-build`, `git`, `curl`, `zip`, `unzip`, `tar`, `pkg-config` and `python3`;
add `clang` for Clang and `libc++-dev` for libc++. Ubuntu 24.04's default GCC 13
and Clang 18 are below the floors, and an upgraded Clang alone is not enough there
because libstdc++ 13 stays below the library floor. On macOS install Xcode or the
Command Line Tools and CMake, Ninja and `pkg-config` (for example, via Homebrew).
vcpkg's FFmpeg port uses the host `pkg-config` on both systems; it does not
download one.
Prepare vcpkg outside this repository:

```sh
git clone https://github.com/microsoft/vcpkg.git ../vcpkg
../vcpkg/bootstrap-vcpkg.sh -disableMetrics
export VCPKG_ROOT="$(cd ../vcpkg && pwd)"
```

The checkout needs the history that contains the pinned baseline; a full clone
has it. The first configure of each build directory builds the dependencies; set
`VCPKG_DEFAULT_BINARY_CACHE` to reuse them across build directories and checkouts.

### Dependencies (vcpkg)

Third-party dependencies are declared only in [`vcpkg.json`](vcpkg.json), in
manifest mode, at the full-SHA `builtin-baseline`. The presets install them
during configure into `build/<preset>/vcpkg_installed`. Features keep the
portable core free of media libraries:

| vcpkg feature | Selected when | Dependencies |
|---------------|---------------|--------------|
| (core) | always | Abseil |
| `tests` | `ALUMBRA_BUILD_TESTS=ON` | GoogleTest, Google Benchmark |
| `desktop` | `ALUMBRA_PORT=desktop` | dav1d, FFmpeg (`avcodec`, `avformat`, `swresample`; default features off), SDL3 (default features off) |

`CMakeLists.txt` selects the features from those options before `project()`, so
the presets do not set `VCPKG_MANIFEST_FEATURES`. The `core` workflow
(`ALUMBRA_PORT=none`, tests off) installs only the core dependencies and builds
Umbra and the Penumbra interface, proving they do not need desktop media
libraries.

Overlay triplets in [`cmake/triplets/`](cmake/triplets/) (`arm64-osx`,
`x64-osx`, `arm64-linux`, `x64-linux`) keep vcpkg's default static linkage but
build FFmpeg as shared libraries, as [ADR-0014](docs/adr/0014-dependency-and-licensing-policy.md)
requires. `ffmpeg_license_test` checks that each FFmpeg library is loaded from
its own shared object and reports `LGPL version 2.1 or later`. The stock FFmpeg
port is for local development only; the narrowed distributable build is #85's.
SDL3 backends and other features are selected by the tasks that use them (D-19).

To add a dependency:

1. Confirm its licence class and platform conditions under ADR-0014; conditional
   and unknown licences need a recorded review before use.
2. Add it to the narrowest feature that needs it in `vcpkg.json`, keeping
   `"default-features": false` unless the defaults are reviewed, and keep the file
   in `vcpkg format-manifest` layout.
3. Find and link it through its CMake package; never from Umbra if it is
   platform-specific.
4. Record it in the third-party inventory (#40).

The licence job regenerates and checks the dependency closure for the native CI
inputs `x64-linux` and `arm64-osx`, with `desktop` and `tests` selected and the
core dependencies always present. The overlay triplets `x64-osx` and
`arm64-linux` are not currently CI inventory inputs; a successful run on one
triplet does not establish coverage for them. The generator reads each
installed port's SPDX 2.3 `licenseConcluded` and copyright file, resolves
ownership through vcpkg's installed graph, classifies build dependencies from
the pinned port manifests' `host` fields, includes the port's declared purpose
and measures linkage from installed library files.

To regenerate one supported triplet after checking out the exact vcpkg
`builtin-baseline`, install its graph and run the matching command:

```sh
VCPKG_ROOT=build/tools/vcpkg
"$VCPKG_ROOT/vcpkg" install --triplet=arm64-osx --overlay-triplets=cmake/triplets \
  --x-feature=desktop --x-feature=tests --x-no-default-features \
  --x-install-root=build/licence
python3 tools/ci/third_party.py generate --triplet arm64-osx \
  --installed-root build/licence --vcpkg-root "$VCPKG_ROOT" \
  --manifest vcpkg.json --policy tools/ci/licence_policy.json \
  --output THIRD_PARTY.md --feature desktop --feature tests
```

Use `--triplet x64-linux` for the Linux input. The policy is in
[`tools/ci/licence_policy.json`](tools/ci/licence_policy.json); exceptions are
exact-version, triplet, feature, expression and linkage records backed by
evidence. The generator reports missing package-level license metadata and
does not infer an SPDX expression from the `licenseDeclared` field or from
copyright text.

A dependency that only runs on the build host and is never linked into or
distributed with Alumbra can be recorded with this classification:

```json
{"classification": "build-time host tool, not distributed"}
```

Such an exception must have the `build` role only and record `reviewer`,
`reviewed_on` (an ISO date), `decision`, `obligations` and the SHA-256 of the
installed copyright file in `copyright_sha256`. Its
`licence_texts` map each `LicenseRef-` identifier used in `resolved_expression`
to the upstream licence `text`, its https `source` and the `sha256` of that
text; the check requires the text to appear verbatim in the installed
copyright file. Those identifiers pass only inside that exception, and denied
identifiers still fail. Document the review in `THIRD_PARTY.md`, as for
`pkgconf`.

To bump the baseline, set `builtin-baseline` to a full vcpkg commit SHA, rebuild
every preset and re-run the licence review for any changed port. Dependabot
proposes these bumps ([dependency updates](docs/process.md#dependency-updates)):
its pull request moves `builtin-baseline` to the full commit SHA of a newer vcpkg
release tag and runs the full CI matrix, which rebuilds the `debug`, `release`,
`asan` and `tsan` presets from the new baseline. CI does not run the `core`
workflow, so build it locally (`cmake --workflow --preset core`) before merging.
Also complete the licence review of every changed port in the same pull request. Dependabot orders release tags only and cannot place an
untagged baseline commit among them, so
[`.github/dependabot.yml`](.github/dependabot.yml) ignores tags dated on or before
the current baseline commit. A manual bump to a commit that is not a release tag
must raise that bound to the commit's date in the same pull request; otherwise
Dependabot proposes moving back to the latest, older tag.
`overrides` are rejected until a recorded rationale exists.
`vcpkg_manifest_pinning_test` (`cmake -P cmake/check_vcpkg_manifest.cmake`)
enforces this: a full-SHA baseline, no overrides, registries only of kind
`builtin` or `git` with full-SHA baselines, overlay paths inside the
repository, and FFmpeg only with default features off and features from
`avcodec`, `avformat` and `swresample`.

```sh
cmake --list-presets
cmake --workflow --preset debug
cmake --workflow --preset release
cmake --workflow --preset asan
cmake --workflow --preset tsan
cmake --workflow --preset core
```

For fully instrumented local dependencies, select the matching sanitizer
overlay triplet at configure time, as the CI sanitizer jobs do:

```sh
cmake --preset asan -DVCPKG_TARGET_TRIPLET=arm64-osx-asan
cmake --preset asan -DVCPKG_TARGET_TRIPLET=x64-linux-asan
cmake --preset tsan -DVCPKG_TARGET_TRIPLET=x64-linux-tsan
```

All four desktop presets use Ninja, have separate `build/<preset>/` directories and
produce `compile_commands.json`. ASan also enables UBSan; TSan is separate.
ASan/UBSan builds use `-fno-sanitize-recover=undefined` and
`-fno-omit-frame-pointer`, so a UBSan finding aborts the test instead of only
printing a diagnostic. The `asan` and `tsan` CI legs use distinct overlay
triplets and vcpkg cache identities, so Abseil, GoogleTest, Google Benchmark
and their transitive dependencies are built with matching instrumentation.
`ALUMBRA_SANITIZER_CANARIES` defaults to `OFF`; CI enables it only after the
normal sanitizer build and suite, then checks that heap-buffer-overflow and
signed-overflow canaries trigger ASan/UBSan and a two-thread data race triggers
TSan. These are isolated test executables and are never built in normal presets.
The presets are visible only on Darwin and Linux. Sanitizer execution depends on
the host's compiler/runtime support. On macOS they set `CMAKE_OSX_SYSROOT=macosx`
so the compiler searches only the SDK: without an explicit sysroot, Apple Clang
searches `/usr/local/include` before the `-isystem` directories of vcpkg packages,
and headers from an Intel Homebrew prefix (for example an older Abseil or FFmpeg)
would silently replace the pinned ones.

The `fuzz` preset builds the libFuzzer targets with ASan and UBSan, without the
desktop port, and runs each corpus replay and each target's fixed-budget smoke
run. It needs upstream Clang with libFuzzer, which Apple Clang does not ship:
use Linux with the LLVM major in `tools/ci/llvm-major` and its compiler-rt
package. Homebrew LLVM works on macOS only where its runtimes support the
host. See [fuzz targets](test/fuzz/README.md) for the helper, the budget, the
canaries and the macOS limits:

```sh
CC=clang-21 CXX=clang++-21 cmake --workflow --preset fuzz
```

Configuration prints the detected toolchain, for example
`Alumbra toolchain: AppleClang 21.0.0 (Xcode 27.2), libc++ 220106, __cplusplus 202302L`,
and fails with a message naming the floor when the compiler, its standard library
or the Xcode/Command Line Tools version is older. Those are the **requested**
floors. The **verified** floors are the exact toolchains whose probe results
[`docs/cpp-features.md`](docs/cpp-features.md) records; a requested floor that
has not been probed does not make any feature usable.

Alumbra's own targets build as C++23. Installed public headers must stay C++20:
which C++23 features implementation files may use is decided feature by feature
in [`docs/cpp-features.md`](docs/cpp-features.md), and nothing is allowed yet.

Each desktop workflow configures, builds and runs CTest, including the
GoogleTest-based `umbra_base_smoke_test.*`, `umbra_base_unittests.*` and
`ffmpeg_license_test.*` cases (registered with `gtest_discover_tests`),
the `fuzz_replay.*` corpus replays of every fuzz target,
`vcpkg_manifest_pinning_test` and its `vcpkg_manifest_rejects_*` fixtures,
`fake_clock_sleep_lint` (tests that use `FakeTickClock` or
`TestTaskEnvironment` may not sleep in real time; see
[threading](docs/architecture/threading.md)), a short `task_runner_benchmark`
smoke run (label `benchmark`) and `alumbra_installed_consumer`. The
`core` workflow configures and builds only. The installed consumer installs to a
private build-tree prefix, builds a separate `find_package(Alumbra)` consumer,
and runs its link probe. It uses installed headers/archives, not `add_subdirectory`
or the source-tree include paths. The consumer reuses the parent build's
toolchain file, compiler, sysroot, macOS architecture/deployment target and
compiler/linker flags, including each configuration's `_<CONFIG>` flags, so both
builds target the same platform. With tests enabled, `CMAKE_INSTALL_LIBDIR` and
`CMAKE_INSTALL_INCLUDEDIR` must be relative paths that stay inside the install
prefix (no leading `..` after normalization); configuration fails otherwise
rather than installing outside the private prefix.

The workflows also run the `layering` tests, which enforce the layer dependency
rule on includes and on the configured link graph
([layering checks](docs/architecture/layering.md)). Run only those with
`ctest --preset debug -L layering`.

The consumer compiles every installed public header in its own translation unit
at exactly C++20 (`static_assert(__cplusplus == 202002L)`), so a newer compiler
default cannot hide a C++23 dependency.
`alumbra_installed_consumer_rejects_cxx23_header` proves the check by adding a
fixture header that uses `std::expected` and requiring that build to fail.

Installed headers may include only the C++20 standard library and other
installed Alumbra headers: no Abseil and no internal engine header
([public headers](docs/architecture/overview.md#public-headers)). Put new
public headers under a module's `include/` and internal ones under its
`internal_include/`, which is not installed. The consumer scans every installed
header's includes (`.h`, `.hpp`, `.hh` and `.hxx`), and the
`alumbra_installed_consumer_rejects_abseil*_header` fixtures prove that a header
including Abseil fails for every supported extension, naming the header and the include.

Fallible engine functions use nodiscard Abseil statuses and the typed helpers
in [error conventions](docs/architecture/errors.md). Umbra and its base unit
tests build without exceptions. The committed error catalogue is generated
with `python3 tools/errors/generate.py` (Python 3.11+); CI rejects stale outputs.
The exported static-library package also requires the producer's detected Abseil
version exactly, because its release-specific symbols remain a link-time ABI
dependency even without public Abseil headers. Set `absl_DIR` to that release's
package directory or rebuild Alumbra with the consumer's release.
`alumbra_installed_consumer_rejects_abseil_version` verifies an incompatible
release fails during configuration; the normal installed consumer verifies the
matching release configures and links.
`alumbra_toolchain_floor_test` checks the floor rules with below- and at-floor
toolchain descriptions.

The `cxx_probe` label runs `tools/cxx-probe` with the build's toolchain and
writes `build/<preset>/test/cxx-probe/cxx-probe-results.json`:

```sh
ctest --preset debug -L cxx_probe --output-on-failure
```

An allowed feature that fails its probe fails the run; unsupported pending or
banned features are reported without failing it. See
[`docs/cpp-features.md`](docs/cpp-features.md) for running the probes against
other toolchains and for how a feature changes status.

### Design tokens

The design tokens in `design/tokens/` generate CSS, TypeScript, Swift, Kotlin
and C++ outputs, which are committed. After editing a token, regenerate them
with `python3 tools/design/generate_tokens.py` (Python 3.9+, standard library
only); never edit a generated file. The C++ header is the header-only target
`alumbra::design_tokens`, whose `design_tokens_test` and `design_tokens_golden`
CTest cases run with the desktop workflows. The other checks need Node.js 24,
a JDK, the Kotlin compiler and, on macOS, Swift; the tools are pinned in
`tools/design/package-lock.json` and the CI workflow.
[Generated outputs](design/README.md#generated-outputs) explains the outputs,
the unit mapping and colour handling, and lists the commands.

### Formatting and lint

LLVM's major is pinned in `tools/ci/llvm-major`, shared by CI's Linux Clang
compiler, clang-format and clang-tidy. Install that LLVM release and put its tools
on `PATH`, or set `LLVM_BIN_DIR` to its `bin` directory (for example the directory
reported by `brew --prefix llvm`, followed by `/bin`). The helpers reject other
majors and print the actual versions:

```sh
scripts/format
scripts/format --check
scripts/tidy -j 2
```

Formatting includes tracked and untracked C/C++/Objective-C sources and headers,
including negative test fixtures and feature probes, but excludes build outputs,
generated files and third-party trees. Check mode uses `--dry-run --Werror` and
prints unified diffs. The optional hook checks the whole working tree, not just
staged files; install it only if no hook is already installed:

```sh
ln -s "$PWD/scripts/pre-commit" "$(git rev-parse --git-path hooks)/pre-commit"
```

Configure and build `debug` before running tidy: the helper reads its compilation
database and lints every repository-owned translation unit, including Umbra,
the desktop port and tests, with all warnings treated as errors. Dependencies
and generated source/header diagnostics are excluded. On macOS it uses Xcode's
Clang resource headers with the SDK from the compilation database.

The profile enables `bugprone`, `cert`, `clang-analyzer`, `concurrency`,
`cppcoreguidelines`, `google`, `misc`, `modernize`, `performance`,
`portability` and `readability`, minus checks that conflict with idioms the
Google C++ Style Guide accepts or with Alumbra's design. `.clang-tidy` cannot
carry comments, so the reasons live here:

- `performance-enum-size`: shrinking enum underlying types would change public
  types for a negligible saving.
- `cppcoreguidelines-avoid-do-while`: `do { … } while (false)` is the standard
  statement-macro idiom.
- `bugprone-macro-parentheses`: `ALUMBRA_ASSIGN_OR_RETURN` takes a declaration
  (`auto value`) as its left-hand side, which cannot be parenthesized.
- `misc-no-recursion`: error-cause chains are encoded recursively with a depth
  bound.
- `misc-` and `cppcoreguidelines-non-private-member-variables-in-classes`:
  plain value types such as `ErrorDetail` expose data members, as Google style
  structs do.
- `clang-analyzer-optin.core.EnumCastOutOfRange`: `ErrorCode` is an open set;
  codes from newer catalogues are decoded into it and must be representable.
- Readability and magic-number checks that duplicate Google style guidance.

Function-like macros are allowed only with the `ALUMBRA_` prefix. Fix findings
in code rather than suppressing them: `NOLINT` markers are comments, which
Alumbra-owned code does not contain, and generated files are fixed in their
generator.

The feature-policy check also replays those commands in strict C++20
syntax-only mode, while the real build stays C++23. This conservative check
rejects opt-in C++23 language/library features, including unlisted ones and
uses hidden behind aliases or macros. It also preprocesses each owned
translation unit as C++20 and as C++23 and fails when the owned source lines
that survive preprocessing differ, so no branch gated on the language version
or a feature-test macro can hide from the C++20 replay. Modules, `[[assume]]` and the Apple-floor
gated `std::format` family are additionally rejected in owned sources.
Feature probes and deliberately invalid `test/build` fixtures are exempt from
feature policy only, not formatting. Since no C++23 feature is allowed today,
adding an allowed row must update this fail-closed gate with that feature's
reviewed probe evidence; it does not silently switch the entire replay to C++23.

Mechanical formatting belongs in its own commit, recorded in
`.git-blame-ignore-revs`. Enable it locally with
`git config blame.ignoreRevsFile .git-blame-ignore-revs`.

### Individual build steps

```sh
cmake --preset debug -DALUMBRA_BUILD_UI=ON -DALUMBRA_WARNINGS_AS_ERRORS=ON
cmake --build --preset debug
ctest --preset debug
cmake --install build/debug --prefix "$PWD/out/install"
```

`ALUMBRA_BUILD_TESTS` defaults to `ON`; `ALUMBRA_PORT` accepts `desktop` (the
default) or `none`, which builds only Umbra and the Penumbra interface. `ALUMBRA_WARNINGS_AS_ERRORS` defaults to `ON` when `CI` is set to a
nonempty value other than `false` or `0`, and otherwise to `OFF`.
`ALUMBRA_BUILD_UI` defaults to `OFF` and adds only the `umbra::ui` skeleton when
enabled. Ads will receive their own opt-in option when that module arrives.
`ALUMBRA_SANITIZER` accepts `none`, `address` or `thread`.
`ALUMBRA_CONFORMANCE_ASSETS` defaults to `OFF`, which registers the
`conformance.asset.*` tests as disabled; `ON` runs them, fetching or generating
their media ([conformance assets](test/conformance/README.md#ctest-labels)).
`ALUMBRA_FUZZING` defaults to `OFF`; it requires tests, `ALUMBRA_SANITIZER=address`
and upstream Clang, and adds the libFuzzer executables and `fuzz_smoke.*` tests.
`ALUMBRA_FUZZ_RUNS`, `ALUMBRA_FUZZ_SEED`, `ALUMBRA_FUZZ_RSS_LIMIT_MB` and
`ALUMBRA_FUZZ_TIMEOUT` hold the smoke budget, and `ALUMBRA_FUZZ_CANARIES`
(default `OFF`) adds the deliberately failing fuzz canaries CI checks.
These are build switches, not runtime settings.

For a manual build without the presets, Abseil must be findable, because
`umbra::base` depends on it; turn tests off unless GoogleTest, Google Benchmark
and FFmpeg are available too. On macOS, pass the SDK sysroot for the reason
above:

```sh
cmake -S . -B build/manual -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DALUMBRA_BUILD_TESTS=OFF -DCMAKE_PREFIX_PATH=/path/to/abseil \
  -DCMAKE_OSX_SYSROOT=macosx
cmake --build build/manual
```

Local overrides belong in the git-ignored `CMakeUserPresets.json`. For example:

```json
{
  "version": 8,
  "configurePresets": [
    {
      "name": "local-debug",
      "inherits": "debug",
      "cacheVariables": { "ALUMBRA_BUILD_UI": true }
    }
  ]
}
```

The installable package currently exports `umbra::base`, `penumbra::interface`,
`penumbra::desktop` (with `ALUMBRA_PORT=desktop`), the header-only
`alumbra::design_tokens` and optionally `umbra::ui`. None
of them links a third-party package yet, so the package config declares no
dependencies; the installed-consumer test fails if an exported target starts
linking anything beyond `penumbra::interface`, or if `alumbra::design_tokens`
links anything at all. Version `0.0.0`, exact-version
matching and these skeleton coordinates are provisional pending D-34. The
temporary `umbra::base::detail` link probe is not a stable engine API.
`antumbra/apps/alumbra-cli/` is reserved for the reference app; no executable or
playback behaviour is implemented yet.

## C++ API reference

The [C++ API reference](https://luisahumadamartens.github.io/alumbra/api/cpp/) covers exactly the installed
headers. Doxygen 1.18.0 builds it from those headers and from Markdown sidecars in
[`docs/api/cpp/`](docs/api/cpp/); sources never carry documentation comments
([ADR-0022](docs/adr/0022-documentation-site-and-api-reference.md)). Each installed header has a sidecar at the
same relative path with a `.md` extension, for example `docs/api/cpp/penumbra/interface/rational.md` for
`penumbra/interface/rational.h`, and `docs/api/cpp/index.md` is the main page.

A sidecar is a sequence of entries. Each entry starts at the beginning of a line with a Doxygen structural
command naming the symbol, followed by its description; the first sentence becomes the summary:

```text
\fn static constexpr std::optional<Rational> penumbra::Rational::Create(int64_t numerator, int64_t denominator)
Creates a normalised fraction, or `std::nullopt` when it cannot be represented.

Returns `std::nullopt` for a zero denominator or when either term is `INT64_MIN`.
```

- Use `\file`, `\namespace`, `\class`, `\struct`, `\enum`, `\typedef`, `\var` (variables, data members and
  enumerators) and `\fn` (functions, constructors, operators and hidden friends, qualified with their class).
  Copy `\fn` declarations from the header; every overload needs its own entry.
- `docs/api/cpp/umbra/base/error_codes.md` is generated with the error catalogue by
  `python3 tools/errors/generate.py`; do not edit it by hand.
- Examples are whole programs in [`test/consumer/examples/`](test/consumer/examples/), included with
  `\include <name>.cc`. `alumbra_installed_consumer` builds every file there against the installed package at
  exactly C++20 and runs it as a test, so an example that stops compiling or returns nonzero fails CI. Sidecars
  cannot contain code blocks, `\code`, `\snippet`, `\dontinclude` or lines indented as code.
- Internal symbols are excluded by the inputs, not by markers: the Doxyfile reads only the installed include
  directories, and `EXCLUDE_SYMBOLS` drops their `internal` and `detail` namespaces.
  `alumbra_installed_consumer` fails when the Doxyfile `INPUT` and the installed headers differ, as
  `alumbra_installed_consumer_rejects_unreferenced_header` proves.

Install the pinned Doxygen, then build the reference into `build/api/cpp/` (or another directory with
`--output`) and run the gate's own tests:

```sh
python3 tools/cpp-reference/doxygen.py --install-dir build/tools/doxygen
export DOXYGEN="$PWD/build/tools/doxygen/1.18.0/doxygen"
python3 tools/cpp-reference/build_reference.py
python3 -m unittest discover -s tools/cpp-reference -p 'test_*.py'
```

The installer downloads the Linux x86-64 or macOS arm64 release and verifies its SHA-256; on other hosts, pass a
Doxygen 1.18.0 binary with `--doxygen` or `DOXYGEN`. The build rejects any other Doxygen version. It fails and
lists each problem when:

- a public declaration has no entry, or its entry has no text (`EXTRACT_ALL` off, `WARN_IF_UNDOCUMENTED` on and
  every warning fatal), so extracting a declaration never counts as documenting it;
- an entry matches no declaration, such as a removed function, overload, enumerator, class or namespace, or
  documents a symbol twice;
- an installed header, namespace or enumerator is undocumented;
- an `\include` names a file outside `test/consumer/examples/`, or Doxygen reports any other warning.

Doxygen's Markdown parser cannot read `\fn` declarations, so
[`tools/cpp-reference/sidecar_filter.py`](tools/cpp-reference/sidecar_filter.py) presents each sidecar to
Doxygen's C++ parser as one documentation block per entry, keeping line numbers. Doxygen does not warn about
undocumented files, namespaces or enumerators, nor about a `\class` or an extra overload entry that matches
nothing, so [`build_reference.py`](tools/cpp-reference/build_reference.py) checks those in Doxygen's XML output.
Doxygen does attach an entry with a wrong signature to a class member that has no overloads; the page then
shows the declared signature.

The reference uses [doxygen-awesome-css](https://github.com/jothepro/doxygen-awesome-css) 2.5.0, vendored
unmodified in `tools/cpp-reference/third_party/` and recorded in [`THIRD_PARTY.md`](THIRD_PARTY.md).

## Code of conduct

Participation is governed by the [Code of Conduct](CODE_OF_CONDUCT.md).
