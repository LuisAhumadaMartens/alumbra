# Third-party dependencies

The vcpkg inventory below is generated from every installed port in the selected
`core`, `desktop` and `tests` graphs, including transitive dependencies. It uses
the effective package `licenseConcluded` from each vcpkg SPDX 2.3 document and
requires the matching copyright file. The policy check intentionally does not
treat `licenseDeclared`, an absent field or copyright wording as an SPDX
approval.

<!-- BEGIN GENERATED VCPKG INVENTORY -->

### `arm64-osx`

Manifest baseline: `9a5dfef5d1a0438a7d24856a85bd60f309d355e9`; installed with `core`, `desktop` and `tests`.
Rows include the complete installed dependency closure. Linkage is detected from the installed package file list (`.a`/`.lib` versus `.dylib`/`.so`), not inferred from the triplet. `build tool` denotes host/build-only dependency paths; it is not a claim that the package is linked into an Alumbra artefact.

| Port | Version | SPDX expression / reviewed terms | Installed linkage | Owning feature | Role | Purpose | Upstream/source |
|------|---------|-----------------------------------|-------------------|----------------|------|---------|-----------------|
| `abseil` | `20260107.1#3` | `Apache-2.0` | static | core | target dependency | Abseil is an open-source collection of C++ library code designed to augment the C++ standard library. The Abseil library code is collected from Google's own C++ code base, has been extensively tested and used in production, and is the same code we depend on in our daily coding lives. | [source](https://github.com/abseil/abseil-cpp) |
| `benchmark` | `1.9.5` | `Apache-2.0` | static | tests | target dependency | A library to benchmark code snippets, similar to unit tests. | [source](https://github.com/google/benchmark) |
| `dav1d` | `1.5.4` | `BSD-2-Clause AND ISC` | static | desktop | target dependency | dav1d is a new open-source AV1 decoder developed by the VideoLAN and FFmpeg communities and sponsored by the Alliance for Open Media. | [source](https://code.videolan.org/videolan/dav1d) |
| `ffmpeg` | `9.0.2#2` | `LicenseRef-vcpkg-null (reviewed as LGPL-2.1-or-later, exercised under LGPL 2.1; ADR-0014, FFmpeg staging approval; source metadata is recorded as LicenseRef-vcpkg-null and the installed copyright selects LGPL 2.1 terms.)` | dynamic | desktop | target dependency | A library to decode, encode, transcode, mux, demux, stream, filter and play pretty much anything that humans and machines have created. | [source](https://ffmpeg.org) |
| `ffmpeg-bin2c` | `9.0.2` | `MIT` | not applicable | desktop | build tool | Host tool bin2c for FFmpeg build | [source](https://github.com/ffmpeg/ffmpeg@n9.0.2) |
| `gtest` | `1.18.0` | `BSD-3-Clause` | static | tests | target dependency | Google Testing and Mocking Framework | [source](https://github.com/google/googletest) |
| `pkgconf` | `3.0.3#1` | `LicenseRef-vcpkg-null (unresolved; policy check fails)` | static | desktop | build tool | pkgconf is a program which helps to configure compiler and linker flags for development libraries. It is similar to pkg-config from freedesktop.org. | [source](https://github.com/pkgconf/pkgconf) |
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
and fails CI until a review permitted by ADR-0014 is recorded. In particular,
the vcpkg `pkgconf` port at `3.0.3#1` currently reports
`LicenseRef-vcpkg-null` in `share/pkgconf/vcpkg.spdx.json`; the pinned port
manifest has `"license": null`. Its `share/pkgconf/copyright` says, "Permission
to use, copy, modify, and/or distribute this software for any purpose with or
without fee is hereby granted," and the installed package contains a static
`libpkgconf.a` plus pkg-config tools. This is a build-only dependency path, but
there is no recorded Alumbra review establishing an SPDX identifier or an
approved exception, so the policy gate remains failing for `pkgconf` on both
inventoried triplets. Do not infer approval or copy that wording into an
exception without a recorded review.

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

PyYAML is installed into an isolated CI environment, not linked into or shipped
with Alumbra. Its distribution includes its MIT licence; preserve that licence
if redistributing the tool. MIT is automatically eligible under ADR-0014.

Test-media licences are out of scope here and tracked by #64.
