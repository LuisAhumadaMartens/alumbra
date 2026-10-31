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
- Respect the layer rule: **Umbra must not include platform headers**; ports never depend on each other.
- New options belong in the settings schema, never as hard-coded constants.
- New dependencies require a licence entry and must comply with [ADR-0010](docs/adr/0010-license-and-visibility.md).
- Add tests: unit tests always; conformance/golden tests for media behaviour; fuzz targets for parsers.
- Commit messages: imperative mood, ≤ 72-character subject, body explains *why*.

## Building the desktop skeleton

Prerequisites for macOS arm64 and Ubuntu LTS:

- A C++20 compiler (Apple Clang on macOS; GCC or Clang on Ubuntu).
- CMake **3.28 or newer** (preset schema version 8) and Ninja.
- Git and a bootstrapped vcpkg checkout. Set `VCPKG_ROOT` to its absolute path;
  presets load `$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake`.

For example, on Ubuntu 24.04 LTS install `build-essential`, `cmake`,
`ninja-build`, `git`, `curl`, `zip`, `unzip` and `tar`. On macOS install the
Xcode command-line tools and CMake/Ninja (for example, via Homebrew).
Prepare vcpkg outside this repository:

```sh
git clone https://github.com/microsoft/vcpkg.git ../vcpkg
../vcpkg/bootstrap-vcpkg.sh -disableMetrics
export VCPKG_ROOT="$(cd ../vcpkg && pwd)"
```

No manifest or third-party packages are used yet; manifest mode is explicitly
disabled until #38. No baseline or package coordinates are selected here.

```sh
cmake --list-presets
cmake --workflow --preset debug
cmake --workflow --preset release
cmake --workflow --preset asan
cmake --workflow --preset tsan
```

All four presets use Ninja, have separate `build/<preset>/` directories and
produce `compile_commands.json`. ASan also enables UBSan; TSan is separate.
The presets are visible only on Darwin and Linux. Sanitizer execution depends on
the host's compiler/runtime support.

Each workflow configures, builds and runs CTest, including
`umbra_base_smoke_test` and `alumbra_installed_consumer`. The latter installs to a
private build-tree prefix, builds a separate `find_package(Alumbra)` consumer,
and runs its link probe. It uses installed headers/archives, not `add_subdirectory`
or the source-tree include paths.

Individual steps and build switches:

```sh
cmake --preset debug -DALUMBRA_BUILD_UI=ON -DALUMBRA_WARNINGS_AS_ERRORS=ON
cmake --build --preset debug
ctest --preset debug
cmake --install build/debug --prefix "$PWD/out/install"
```

`ALUMBRA_BUILD_TESTS` defaults to `ON`; `ALUMBRA_PORT` currently accepts only
`desktop`. `ALUMBRA_WARNINGS_AS_ERRORS` defaults to `ON` when `CI` is set to a
nonempty value other than `false` or `0`, and otherwise to `OFF`.
`ALUMBRA_BUILD_UI` defaults to `OFF` and adds only the `umbra::ui` skeleton when
enabled. Ads will receive their own opt-in option when that module arrives.
`ALUMBRA_SANITIZER` accepts `none`, `address` or `thread`.
These are build switches, not runtime settings.

For a dependency-free manual build without a vcpkg checkout:

```sh
cmake -S . -B build/manual -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/manual
ctest --test-dir build/manual --output-on-failure
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
`penumbra::desktop` and optionally `umbra::ui`. Version `0.0.0`, exact-version
matching and these skeleton coordinates are provisional pending D-34. The
temporary `umbra::base::detail` link probe is not a stable engine API.
`antumbra/apps/alumbra-cli/` is reserved for the reference app; no executable or
playback behaviour is implemented yet.

## Code of conduct

Participation is governed by the [Code of Conduct](CODE_OF_CONDUCT.md).
