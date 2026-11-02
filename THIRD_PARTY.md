# Third-party dependencies

The vcpkg inventory below is generated from every installed port in the selected
`core`, `desktop` and `tests` graphs, including transitive dependencies. It uses
the effective package `licenseConcluded` from each vcpkg SPDX 2.3 document and
requires the matching copyright file. The policy check intentionally does not
treat `licenseDeclared`, an absent field or copyright wording as an SPDX
approval.

<!-- BEGIN GENERATED VCPKG INVENTORY -->

### `arm64-osx`

Manifest baseline: `9a5dfef5d1a0438a7d24856a85bd60f309d355e9`; selected dependency roots: `core`, `desktop`, `tests`.
Rows include the complete installed dependency closure. Linkage is detected from the installed package file list (`.a`/`.lib` versus `.dylib`/`.so`), not inferred from the triplet. `build tool` denotes host/build-only dependency paths; it is not a claim that the package is linked into an Alumbra artefact.

| Port | Version | SPDX expression / reviewed terms | Installed linkage | Owning feature | Role | Purpose | Upstream/source |
|------|---------|-----------------------------------|-------------------|----------------|------|---------|-----------------|
| `abseil` | `20260107.1#3` | `Apache-2.0` | static | core | target dependency | Abseil is an open-source collection of C++ library code designed to augment the C++ standard library. The Abseil library code is collected from Google's own C++ code base, has been extensively tested and used in production, and is the same code we depend on in our daily coding lives. | [source](https://github.com/abseil/abseil-cpp) |
| `benchmark` | `1.9.5` | `Apache-2.0` | static | tests | target dependency | A library to benchmark code snippets, similar to unit tests. | [source](https://github.com/google/benchmark) |
| `dav1d` | `1.5.4` | `BSD-2-Clause AND ISC` | static | desktop | target dependency | dav1d is a new open-source AV1 decoder developed by the VideoLAN and FFmpeg communities and sponsored by the Alliance for Open Media. | [source](https://code.videolan.org/videolan/dav1d) |
| `ffmpeg` | `9.0.2#2` | `LicenseRef-vcpkg-null (reviewed as LGPL-2.1-or-later, exercised under LGPL 2.1; ADR-0014, FFmpeg staging approval; source metadata is recorded as LicenseRef-vcpkg-null and the installed copyright selects LGPL 2.1 terms.)` | dynamic | desktop | target dependency | A library to decode, encode, transcode, mux, demux, stream, filter and play pretty much anything that humans and machines have created. | [source](https://ffmpeg.org) |
| `ffmpeg-bin2c` | `9.0.2` | `MIT` | not applicable | desktop | build tool | Host tool bin2c for FFmpeg build | [source](https://github.com/ffmpeg/ffmpeg@n9.0.2) |
| `gtest` | `1.18.0` | `BSD-3-Clause` | static | tests | target dependency | Google Testing and Mocking Framework | [source](https://github.com/google/googletest) |
| `pkgconf` | `3.0.3#1` | `LicenseRef-vcpkg-null (reviewed as LicenseRef-pkgconf-COPYING, a permissive ISC-style notice whose warranty disclaimer differs from canonical ISC; build-time host tool, not distributed; ADR-0014 reviewed exception by LuisAhumadaMartens on 2026-11-01.)` | static | desktop | build tool | pkgconf is a program which helps to configure compiler and linker flags for development libraries. It is similar to pkg-config from freedesktop.org. | [source](https://github.com/pkgconf/pkgconf) |
| `sdl3` | `3.4.18` | `Apache-2.0 AND BSD-3-Clause AND MIT AND Zlib` | static | desktop | target dependency | Simple DirectMedia Layer is a cross-platform development library designed to provide low level access to audio, keyboard, mouse, joystick, and graphics hardware via OpenGL and Direct3D. | [source](https://www.libsdl.org) |
| `vcpkg-cmake` | `2025-08-07` | `MIT` | not applicable | core, desktop, tests | build tool | Not specified by vcpkg | [source](https://github.com/microsoft/vcpkg/tree/9a5dfef5d1a0438a7d24856a85bd60f309d355e9/ports/vcpkg-cmake) |
| `vcpkg-cmake-config` | `2026-07-21` | `MIT` | not applicable | core, desktop, tests | build tool | Not specified by vcpkg | [source](https://github.com/microsoft/vcpkg/tree/9a5dfef5d1a0438a7d24856a85bd60f309d355e9/ports/vcpkg-cmake-config) |
| `vcpkg-cmake-get-vars` | `2025-05-29` | `MIT` | not applicable | desktop | build tool | Not specified by vcpkg | [source](https://github.com/microsoft/vcpkg/tree/9a5dfef5d1a0438a7d24856a85bd60f309d355e9/ports/vcpkg-cmake-get-vars) |
| `vcpkg-pkgconfig-get-modules` | `2024-04-03` | `MIT` | not applicable | desktop | build tool | Not specified by vcpkg | [source](https://github.com/microsoft/vcpkg/tree/9a5dfef5d1a0438a7d24856a85bd60f309d355e9/ports/vcpkg-pkgconfig-get-modules) |
| `vcpkg-tool-meson` | `1.12.1` | `Apache-2.0 AND MIT` | not applicable | desktop | build tool | Meson build system | [source](https://github.com/mesonbuild/meson) |

### `x64-linux`

Manifest baseline: `9a5dfef5d1a0438a7d24856a85bd60f309d355e9`; selected dependency roots: `core`, `desktop`, `tests`.
Rows include the complete installed dependency closure. Linkage is detected from the installed package file list (`.a`/`.lib` versus `.dylib`/`.so`), not inferred from the triplet. `build tool` denotes host/build-only dependency paths; it is not a claim that the package is linked into an Alumbra artefact.

| Port | Version | SPDX expression / reviewed terms | Installed linkage | Owning feature | Role | Purpose | Upstream/source |
|------|---------|-----------------------------------|-------------------|----------------|------|---------|-----------------|
| `abseil` | `20260107.1#3` | `Apache-2.0` | static | core | target dependency | Abseil is an open-source collection of C++ library code designed to augment the C++ standard library. The Abseil library code is collected from Google's own C++ code base, has been extensively tested and used in production, and is the same code we depend on in our daily coding lives. | [source](https://github.com/abseil/abseil-cpp) |
| `benchmark` | `1.9.5` | `Apache-2.0` | static | tests | target dependency | A library to benchmark code snippets, similar to unit tests. | [source](https://github.com/google/benchmark) |
| `dav1d` | `1.5.4` | `BSD-2-Clause AND ISC` | static | desktop | target dependency | dav1d is a new open-source AV1 decoder developed by the VideoLAN and FFmpeg communities and sponsored by the Alliance for Open Media. | [source](https://code.videolan.org/videolan/dav1d) |
| `ffmpeg` | `9.0.2#2` | `LicenseRef-vcpkg-null (reviewed as LGPL-2.1-or-later, exercised under LGPL 2.1; ADR-0014, FFmpeg staging approval; source metadata is recorded as LicenseRef-vcpkg-null and the installed copyright selects LGPL 2.1 terms.)` | dynamic | desktop | target dependency | A library to decode, encode, transcode, mux, demux, stream, filter and play pretty much anything that humans and machines have created. | [source](https://ffmpeg.org) |
| `ffmpeg-bin2c` | `9.0.2` | `MIT` | not applicable | desktop | build tool | Host tool bin2c for FFmpeg build | [source](https://github.com/ffmpeg/ffmpeg@n9.0.2) |
| `gtest` | `1.18.0` | `BSD-3-Clause` | static | tests | target dependency | Google Testing and Mocking Framework | [source](https://github.com/google/googletest) |
| `pkgconf` | `3.0.3#1` | `LicenseRef-vcpkg-null (reviewed as LicenseRef-pkgconf-COPYING, a permissive ISC-style notice whose warranty disclaimer differs from canonical ISC; build-time host tool, not distributed; ADR-0014 reviewed exception by LuisAhumadaMartens on 2026-11-01.)` | static | desktop | build tool | pkgconf is a program which helps to configure compiler and linker flags for development libraries. It is similar to pkg-config from freedesktop.org. | [source](https://github.com/pkgconf/pkgconf) |
| `sdl3` | `3.4.18` | `Apache-2.0 AND BSD-3-Clause AND MIT AND Zlib` | static | desktop | target dependency | Simple DirectMedia Layer is a cross-platform development library designed to provide low level access to audio, keyboard, mouse, joystick, and graphics hardware via OpenGL and Direct3D. | [source](https://www.libsdl.org) |
| `vcpkg-cmake` | `2025-08-07` | `MIT` | not applicable | core, desktop, tests | build tool | Not specified by vcpkg | [source](https://github.com/microsoft/vcpkg/tree/9a5dfef5d1a0438a7d24856a85bd60f309d355e9/ports/vcpkg-cmake) |
| `vcpkg-cmake-config` | `2026-07-21` | `MIT` | not applicable | core, desktop, tests | build tool | Not specified by vcpkg | [source](https://github.com/microsoft/vcpkg/tree/9a5dfef5d1a0438a7d24856a85bd60f309d355e9/ports/vcpkg-cmake-config) |
| `vcpkg-cmake-get-vars` | `2025-05-29` | `MIT` | not applicable | desktop | build tool | Not specified by vcpkg | [source](https://github.com/microsoft/vcpkg/tree/9a5dfef5d1a0438a7d24856a85bd60f309d355e9/ports/vcpkg-cmake-get-vars) |
| `vcpkg-pkgconfig-get-modules` | `2024-04-03` | `MIT` | not applicable | desktop | build tool | Not specified by vcpkg | [source](https://github.com/microsoft/vcpkg/tree/9a5dfef5d1a0438a7d24856a85bd60f309d355e9/ports/vcpkg-pkgconfig-get-modules) |
| `vcpkg-tool-meson` | `1.12.1` | `Apache-2.0 AND MIT` | not applicable | desktop | build tool | Meson build system | [source](https://github.com/mesonbuild/meson) |
<!-- END GENERATED VCPKG INVENTORY -->

## Licence policy and coverage

The machine-readable allowlist, conditional rules and reviewed exceptions are
in [`tools/ci/licence_policy.json`](tools/ci/licence_policy.json), implementing
[ADR-0014](docs/adr/0014-dependency-and-licensing-policy.md) and
[ADR-0010](docs/adr/0010-license-and-visibility.md). `AND` requires every
licence obligation; `OR` requires an exact reviewed branch choice; `WITH`
requires review. GPL, AGPL, LGPL 3 and non-free terms are denied and cannot be waived by a
review. Unresolved or unlisted terms also fail until the accepted policy
contains a version-specific, evidence-backed review that identifies the
effective terms; an unknown expression or metadata error never passes.

The CI inventory inputs are the native runner triplets `x64-linux` and
`arm64-osx`, each installed with `desktop` and `tests` plus the always-on core
dependencies. The repository also declares `x64-osx` and `arm64-linux` overlay
triplets, but those are not currently CI inventory inputs and are not implied
to have been verified by the generated sections.

FFmpeg's `LicenseRef-vcpkg-null` is recorded as interpreted under the
LGPL-2.1-or-later review accepted by ADR-0014, exercised under LGPL 2.1. The
exception is tied to the pinned baseline, package version, selected features,
target triplet, dynamic linkage, SPDX expression, upstream and installed
copyright evidence. This inventory does not verify the stock port's configure
output or make it distributable; the narrowed build and release obligations
remain #85 and D-34.

Any other unresolved expression remains visible in the generated inventory
and fails CI until a review permitted by ADR-0014 is recorded.

### Reviewed exception: `pkgconf` 3.0.3#1

The vcpkg `pkgconf` port reports `LicenseRef-vcpkg-null` in
`share/pkgconf/vcpkg.spdx.json` because its pinned port manifest has
`"license": null`. It is recorded as a reviewed exception in
[`tools/ci/licence_policy.json`](tools/ci/licence_policy.json):

| Field | Record |
|-------|--------|
| Keyed to | vcpkg baseline `9a5dfef5d1a0438a7d24856a85bd60f309d355e9`; port `pkgconf` version `3.0.3#1` with no port features; triplets `arm64-osx` and `x64-linux`; owning feature `desktop`; build role only; installed linkage `static`; SPDX expression `LicenseRef-vcpkg-null`; manifest `"license": null`; upstream `https://github.com/pkgconf/pkgconf` |
| Classification | Build-time host tool, not distributed |
| Reviewed terms | `LicenseRef-pkgconf-COPYING`: a permissive ISC-style notice whose warranty disclaimer differs from the canonical SPDX `ISC` text, so it is not labelled `ISC` |
| Licence text | [`COPYING` at tag `pkgconf-3.0.3`](https://github.com/pkgconf/pkgconf/blob/pkgconf-3.0.3/COPYING), SHA-256 `055a265b839672a62f590869b0a1f8e4f5147e8f073c9ba77b17b8d7ad653966`, reproduced below and in the policy file |
| Installed copyright | `share/pkgconf/copyright`, SHA-256 `dd6b23a2856f36fd4f089687d1cb54e9b3bcb6501ef793685b329921de824918` |
| Use | Runs on the build host only: FFmpeg's port declares `vcpkg-pkgconfig-get-modules` as a host dependency, and that helper runs `tools/pkgconf/pkgconf` to query module flags while FFmpeg builds. Alumbra's own build does not use pkg-config. |
| Obligations | None for Alumbra while no pkgconf executable, `libpkgconf` or `pkg.m4` file is linked into or distributed with an Alumbra artefact |
| Decision | Accepted under ADR-0014 by LuisAhumadaMartens on 2026-11-01 |
| Evidence | [Port, source and build-use evidence on #300](https://github.com/LuisAhumadaMartens/alumbra/pull/300#issuecomment-6555339318) |

```text
Copyright (c) 2011-2026 pkgconf authors (see AUTHORS file in source directory).

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

This software is provided 'as is' and without any warranty, express or
implied.  In no event shall the authors be liable for any damages arising
from the use of this software.
```

The installed copyright file also carries the notices of the getopt
implementation compiled into the pkgconf tools
(`LicenseRef-scancode-sudo AND BSD-4-Clause`), two `BSD-2-Clause` sources, and
`pkg.m4` (GPL-2.0-or-later with the Autoconf exception), which the port installs
under `share/aclocal`. The review covers that exact bundle through its SHA-256;
none of those files reaches an Alumbra artefact.

`LicenseRef-pkgconf-COPYING` is accepted only inside this exception. The policy
check confirms that the recorded text matches its SHA-256 and appears verbatim
in the installed copyright, that the installed copyright matches the reviewed
hash and that `pkgconf` has no target dependency path. A different version or
port version, another baseline, a target dependency path or any other change to
the keyed fields leaves the expression unreviewed, so CI fails with
`block-unresolved-licences`; a changed copyright file or licence text fails the
exception's evidence validation. Either way a new review must be recorded.
Recorded licence texts are honoured only for the build-time host tool
classification and never waive a denied identifier. Shipping any pkgconf file
would also void the classification; the inventory cannot detect that, so
release packaging (D-34) must not include one.

Regenerate both inventory sections using the exact `builtin-baseline` checkout
and separate native installs:

```sh
VCPKG_ROOT=build/tools/vcpkg
"$VCPKG_ROOT/vcpkg" install --triplet=arm64-osx --overlay-triplets=cmake/triplets \
  --x-feature=desktop --x-feature=tests --x-no-default-features \
  --x-install-root=build/licence
python3 tools/ci/third_party.py generate --triplet arm64-osx \
  --installed-root build/licence --vcpkg-root "$VCPKG_ROOT" \
  --manifest vcpkg.json --policy tools/ci/licence_policy.json \
  --output THIRD_PARTY.md --feature desktop --feature tests
"$VCPKG_ROOT/vcpkg" install --triplet=x64-linux --overlay-triplets=cmake/triplets \
  --x-feature=desktop --x-feature=tests --x-no-default-features \
  --x-install-root=build/licence
python3 tools/ci/third_party.py generate --triplet x64-linux \
  --installed-root build/licence --vcpkg-root "$VCPKG_ROOT" \
  --manifest vcpkg.json --policy tools/ci/licence_policy.json \
  --output THIRD_PARTY.md --feature desktop --feature tests
```

## CI-only tools

| Dependency | Version | SPDX licence | Use / source |
|------------|---------|--------------|--------------|
| PyYAML | 6.0.3 | MIT | CI-only YAML parsing and regression tests; pinned in [`tools/ci/requirements.txt`](tools/ci/requirements.txt). [Source and licence](https://github.com/yaml/pyyaml/tree/6.0.3). |
| libFuzzer (LLVM compiler-rt) | The LLVM major in [`tools/ci/llvm-major`](tools/ci/llvm-major) | Apache-2.0 WITH LLVM-exception | Test-only fuzzing runtime linked into the `fuzz` preset's fuzz executables; installed with the pinned Clang (`libclang-rt-<major>-dev` on Ubuntu). [Source and licence](https://github.com/llvm/llvm-project/tree/main/compiler-rt/lib/fuzzer). |
| FFmpeg (Ubuntu `ffmpeg` package) | The runner image's package version | Package-dependent (the Ubuntu build enables GPL components) | CI-only host tool that generates and probes synthetic conformance clips; see [conformance assets](test/conformance/README.md). |
| TypeScript | 7.0.2 | Apache-2.0 | Strict type-check of the generated design-token TypeScript; locked in [`tools/design/package-lock.json`](tools/design/package-lock.json). [Source and licence](https://github.com/microsoft/TypeScript). |
| stylelint | 17.16.0 | MIT | Lints the generated design-token CSS; locked in [`tools/design/package-lock.json`](tools/design/package-lock.json). [Source and licence](https://github.com/stylelint/stylelint). |
| stylelint-config-standard | 40.0.0 | MIT | The stylelint rule set, with the overrides in [`tools/design/stylelint.config.json`](tools/design/stylelint.config.json). [Source and licence](https://github.com/stylelint/stylelint-config-standard). |
| Kotlin compiler | 2.4.21 | Apache-2.0 | Compiles the generated design-token Kotlin; the release archive is pinned by SHA-256 in [`.github/workflows/ci.yml`](.github/workflows/ci.yml). [Source and licence](https://github.com/JetBrains/kotlin/tree/v2.4.21). |
| Doxygen | 1.18.0 | GPL-2.0 | CI and developer tool that generates the C++ API reference; release binaries pinned by SHA-256 in [`tools/cpp-reference/doxygen.py`](tools/cpp-reference/doxygen.py). [Source and licence](https://github.com/doxygen/doxygen/tree/Release_1_18_0). |
| doxygen-awesome-css | 2.5.0 (commit `46483f1e5a70ffb9ecd3b82d0a1cd1b24edf13da`) | MIT | Theme of the published C++ API reference. `doxygen-awesome.css` (SHA-256 `ea37516ec7a7a7780719b9d9a285fe3268a8d5411523d8b93af98d77e67cd1a1`) and `LICENSE` (SHA-256 `e3da754c3f657cc78594fa2e8a3283665f78c743df2485fa9e498a8973051191`) are vendored unmodified in [`tools/cpp-reference/third_party/doxygen-awesome-css/`](tools/cpp-reference/third_party/doxygen-awesome-css/). [Source and licence](https://github.com/jothepro/doxygen-awesome-css/tree/v2.5.0). |

PyYAML is installed into an isolated CI environment, not linked into or shipped
with Alumbra. Its distribution includes its MIT licence; preserve that licence
if redistributing the tool. MIT is automatically eligible under ADR-0014.

libFuzzer ships with the pinned LLVM toolchain, like the sanitizer runtimes, and adds no package to
`vcpkg.json`. It is linked only into fuzz executables, which are never installed or distributed
(see [fuzz targets](test/fuzz/README.md)), so no redistribution condition applies. Apache-2.0 is
automatically eligible under ADR-0014, which asks for `WITH` exceptions to be reviewed; the LLVM exception
only grants extra permission to redistribute object code compiled from the runtime.

The Ubuntu `ffmpeg` package is installed on CI runners and by developers to generate test media. It runs as a
separate host process, is not linked into, installed with or distributed with Alumbra, and its output is
generated locally and never committed, so its GPL components impose no obligation on Alumbra. The runtime FFmpeg
that Alumbra links is a separate vcpkg port, recorded in the generated inventory above.


The design-token tools check generated files in CI and on developer machines; none of them, and none of their
dependencies, is linked into or shipped with Alumbra, and the generated outputs depend on none of them.
`npm ci` installs the exact versions and integrity hashes in the lockfile. Its 139 transitive packages are
licensed MIT (100), Apache-2.0 (21, mostly TypeScript's per-platform compiler packages), ISC (9), MIT-0 (3),
BSD-3-Clause (3), Python-2.0 (`argparse`) and CC0-1.0 (`mdn-data`); `svg-tags` declares MIT in its legacy
`licenses` field. MIT-0, Python-2.0 and CC0-1.0 are permissive but outside ADR-0014's automatic list, which
governs dependencies of Alumbra's artefacts; they are recorded here for that reason. CI also uses Node.js
24.22.2 and the Temurin 21.0.13+9 JDK through the pinned `actions/setup-node` and `actions/setup-java` actions,
and Swift from the pinned Xcode on macOS, as runtimes for these checks only.

Doxygen runs only on CI runners and developer machines, as [ADR-0022](docs/adr/0022-documentation-site-and-api-reference.md)
decides; it is never linked into or shipped with Alumbra. Its licence states that documents it produces are
derivative works of their input and are not affected by that licence. The JavaScript files that Doxygen copies
into the generated HTML carry their own MIT licence notices, which the published reference keeps intact.

doxygen-awesome-css is published with the C++ API reference on the documentation site. The stylesheet keeps its
SPDX identifier and copyright notice, and the build publishes its `LICENSE` beside the reference as
`LICENSE-doxygen-awesome-css.txt`. Keep both when updating the theme, and update the version, commit and
checksums here.

Test-media licences are out of scope here and tracked by #64.
