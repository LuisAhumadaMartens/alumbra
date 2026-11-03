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
| `ffmpeg` | `9.0.2#3` | `LicenseRef-vcpkg-null (reviewed as LGPL-2.1-or-later, exercised under LGPL 2.1; ADR-0014, FFmpeg approval for the narrowed overlay port of #85; source metadata is recorded as LicenseRef-vcpkg-null and the installed copyright selects LGPL 2.1 terms.)` | dynamic | desktop | target dependency | A library to decode, encode, transcode, mux, demux, stream, filter and play pretty much anything that humans and machines have created. | [source](https://ffmpeg.org) |
| `ffmpeg-bin2c` | `9.0.2` | `MIT` | not applicable | desktop | build tool | Host tool bin2c for FFmpeg build | [source](https://github.com/ffmpeg/ffmpeg@n9.0.2) |
| `freetype` | `2.14.3` | `(FTL OR GPL-2.0-or-later) (selected FTL; ADR-0030 and ADR-0014 (#330): FreeType rasterises the desktop UI's glyphs. Alumbra elects the FreeType License (FTL) branch and never the GPL branch, whose terms ADR-0014 denies.)` | static | desktop | target dependency | A library to render fonts. | [source](https://www.freetype.org/) |
| `gtest` | `1.18.0` | `BSD-3-Clause` | static | tests | target dependency | Google Testing and Mocking Framework | [source](https://github.com/google/googletest) |
| `harfbuzz` | `14.6.0` | `MIT AND MIT-Modern-Variant (reviewed as MIT, for the second term of the expression, and the 'Old MIT' grant of the installed copyright, whose only conditions are retaining its copyright notice and its two disclaimer paragraphs in all copies; no GPL or LGPL term is present; ADR-0030 and ADR-0014 (#329): HarfBuzz shapes the desktop UI's text. MIT-Modern-Variant is HarfBuzz's own permissive 'Old MIT' grant, outside the automatic list, reviewed here for this exact port version and feature set. The maintainer accepts this entry by merging the pull request.)` | static | desktop | target dependency | HarfBuzz OpenType text shaping engine | [source](https://github.com/harfbuzz/harfbuzz) |
| `libunibreak` | `7.0` | `zlib-acknowledgement (reviewed as zlib-acknowledgement; retain the copyright and permission notice, and mark altered source versions; ADR-0014 version-specific review for portable text layout; acceptance recorded by merging this issue.)` | static | core | target dependency | an implementation of the line breaking and word breaking algorithms as described in [Unicode Standard Annex 14] 1 and [Unicode Standard Annex 29] 2. Check the project's [home page] 3 for up-to-date information. | [source](https://github.com/adah1972/libunibreak) |
| `pkgconf` | `3.0.3#1` | `LicenseRef-vcpkg-null (reviewed as LicenseRef-pkgconf-COPYING, a permissive ISC-style notice whose warranty disclaimer differs from canonical ISC; build-time host tool, not distributed; ADR-0014 reviewed exception by LuisAhumadaMartens on 2026-11-01.)` | static | desktop | build tool | pkgconf is a program which helps to configure compiler and linker flags for development libraries. It is similar to pkg-config from freedesktop.org. | [source](https://github.com/pkgconf/pkgconf) |
| `sdl3` | `3.4.18` | `Apache-2.0 AND BSD-3-Clause AND MIT AND Zlib` | static | desktop | target dependency | Simple DirectMedia Layer is a cross-platform development library designed to provide low level access to audio, keyboard, mouse, joystick, and graphics hardware via OpenGL and Direct3D. | [source](https://www.libsdl.org) |
| `sheenbidi` | `3.0.0` | `Apache-2.0` | static | core | target dependency | SheenBidi is a lightweight, fast and stable implementation of the Unicode Bidirectional Algorithm. | [source](https://github.com/Tehreer/SheenBidi) |
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
| `alsa` | `1.2.15.3#1` | `LGPL-2.1-or-later (reviewed as LGPL-2.1-or-later, exercised under LGPL 2.1; ADR-0030 and ADR-0014: the Linux audio backend of the desktop sdl3 port (#69). SDL loads libasound at run time, so no ALSA code is linked into an Alumbra artefact, and the library stays a separately replaceable shared object.)` | dynamic | desktop | target and build dependency | The Advanced Linux Sound Architecture (ALSA) - library | [source](https://www.alsa-project.org/) |
| `benchmark` | `1.9.5` | `Apache-2.0` | static | tests | target dependency | A library to benchmark code snippets, similar to unit tests. | [source](https://github.com/google/benchmark) |
| `dav1d` | `1.5.4` | `BSD-2-Clause AND ISC` | static | desktop | target dependency | dav1d is a new open-source AV1 decoder developed by the VideoLAN and FFmpeg communities and sponsored by the Alliance for Open Media. | [source](https://code.videolan.org/videolan/dav1d) |
| `directx-dxc` | `2026-05-27` | `LicenseRef-vcpkg-null (reviewed as LicenseRef-DXC-LICENSE-TXT-NCSA, the University of Illinois/NCSA licence that opens DirectXShaderCompiler's LICENSE.TXT; build-time host tool, not distributed; ADR-0030 and ADR-0014: DXC as a build-only tool. The maintainer accepts this entry by merging the pull request.)` | dynamic | desktop | build tool | DirectX Shader Compiler (LLVM/Clang) | [source](https://github.com/microsoft/DirectXShaderCompiler) |
| `ffmpeg` | `9.0.2#3` | `LicenseRef-vcpkg-null (reviewed as LGPL-2.1-or-later, exercised under LGPL 2.1; ADR-0014, FFmpeg approval for the narrowed overlay port of #85; source metadata is recorded as LicenseRef-vcpkg-null and the installed copyright selects LGPL 2.1 terms.)` | dynamic | desktop | target dependency | A library to decode, encode, transcode, mux, demux, stream, filter and play pretty much anything that humans and machines have created. | [source](https://ffmpeg.org) |
| `ffmpeg-bin2c` | `9.0.2` | `MIT` | not applicable | desktop | build tool | Host tool bin2c for FFmpeg build | [source](https://github.com/ffmpeg/ffmpeg@n9.0.2) |
| `freetype` | `2.14.3` | `(FTL OR GPL-2.0-or-later) (selected FTL; ADR-0030 and ADR-0014 (#330): FreeType rasterises the desktop UI's glyphs. Alumbra elects the FreeType License (FTL) branch and never the GPL branch, whose terms ADR-0014 denies.)` | static | desktop | target dependency | A library to render fonts. | [source](https://www.freetype.org/) |
| `gtest` | `1.18.0` | `BSD-3-Clause` | static | tests | target dependency | Google Testing and Mocking Framework | [source](https://github.com/google/googletest) |
| `harfbuzz` | `14.6.0` | `MIT AND MIT-Modern-Variant (reviewed as MIT, for the second term of the expression, and the 'Old MIT' grant of the installed copyright, whose only conditions are retaining its copyright notice and its two disclaimer paragraphs in all copies; no GPL or LGPL term is present; ADR-0030 and ADR-0014 (#329): HarfBuzz shapes the desktop UI's text. MIT-Modern-Variant is HarfBuzz's own permissive 'Old MIT' grant, outside the automatic list, reviewed here for this exact port version and feature set. The maintainer accepts this entry by merging the pull request.)` | static | desktop | target dependency | HarfBuzz OpenType text shaping engine | [source](https://github.com/harfbuzz/harfbuzz) |
| `libunibreak` | `7.0` | `zlib-acknowledgement (reviewed as zlib-acknowledgement; retain the copyright and permission notice, and mark altered source versions; ADR-0014 version-specific review for portable text layout; acceptance recorded by merging this issue.)` | static | core | target dependency | an implementation of the line breaking and word breaking algorithms as described in [Unicode Standard Annex 14] 1 and [Unicode Standard Annex 29] 2. Check the project's [home page] 3 for up-to-date information. | [source](https://github.com/adah1972/libunibreak) |
| `pkgconf` | `3.0.3#1` | `LicenseRef-vcpkg-null (reviewed as LicenseRef-pkgconf-COPYING, a permissive ISC-style notice whose warranty disclaimer differs from canonical ISC; build-time host tool, not distributed; ADR-0014 reviewed exception by LuisAhumadaMartens on 2026-11-01.)` | static | desktop | build tool | pkgconf is a program which helps to configure compiler and linker flags for development libraries. It is similar to pkg-config from freedesktop.org. | [source](https://github.com/pkgconf/pkgconf) |
| `sdl3` | `3.4.18` | `Apache-2.0 AND BSD-3-Clause AND MIT AND Zlib` | static | desktop | target and build dependency | Simple DirectMedia Layer is a cross-platform development library designed to provide low level access to audio, keyboard, mouse, joystick, and graphics hardware via OpenGL and Direct3D. | [source](https://www.libsdl.org) |
| `sdl3-shadercross` | `3.0.0-preview2` | `Zlib` | static | desktop | build tool | This is a library for translating shaders to different formats, intended for use with SDL's GPU API. It takes SPIRV or HLSL as the source and outputs DXBC, DXIL, SPIRV, MSL, or HLSL. | [source](https://www.libsdl.org) |
| `sheenbidi` | `3.0.0` | `Apache-2.0` | static | core | target dependency | SheenBidi is a lightweight, fast and stable implementation of the Unicode Bidirectional Algorithm. | [source](https://github.com/Tehreer/SheenBidi) |
| `spirv-cross` | `1.4.363.0` | `NOASSERTION (reviewed as Apache-2.0, from the Apache License text in the installed copyright; ADR-0030 and ADR-0014: a build-only SPIR-V tool whose vcpkg metadata is NOASSERTION; the installed copyright records its terms.)` | static | desktop | build tool | SPIRV-Cross is a practical tool and library for performing reflection on SPIR-V and disassembling SPIR-V back to high level languages. | [source](https://github.com/KhronosGroup/SPIRV-Cross) |
| `spirv-headers` | `1.4.357.0` | `NOASSERTION (reviewed as MIT, from the Khronos permission notice in the installed copyright; the Creative Commons Attribution 4.0 text in the same file covers Markdown documentation that the port does not install; ADR-0030 and ADR-0014: a build-only SPIR-V tool whose vcpkg metadata is NOASSERTION; the installed copyright records its terms.)` | not applicable | desktop | build tool | Machine-readable files for the SPIR-V Registry | [source](https://github.com/KhronosGroup/SPIRV-Headers) |
| `vcpkg-cmake` | `2025-08-07` | `MIT` | not applicable | core, desktop, tests | build tool | Not specified by vcpkg | [source](https://github.com/microsoft/vcpkg/tree/9a5dfef5d1a0438a7d24856a85bd60f309d355e9/ports/vcpkg-cmake) |
| `vcpkg-cmake-config` | `2026-07-21` | `MIT` | not applicable | core, desktop, tests | build tool | Not specified by vcpkg | [source](https://github.com/microsoft/vcpkg/tree/9a5dfef5d1a0438a7d24856a85bd60f309d355e9/ports/vcpkg-cmake-config) |
| `vcpkg-cmake-get-vars` | `2025-05-29` | `MIT` | not applicable | desktop | build tool | Not specified by vcpkg | [source](https://github.com/microsoft/vcpkg/tree/9a5dfef5d1a0438a7d24856a85bd60f309d355e9/ports/vcpkg-cmake-get-vars) |
| `vcpkg-make` | `2026-07-09#2` | `LicenseRef-vcpkg-null (reviewed as MIT, the vcpkg licence that covers the CMake scripts; the installed copyright also carries the GPL-2.0-or-later text of the Automake scripts, which stay on the build host; build-time host tool, not distributed; ADR-0014 reviewed exception for the Linux audio backend (#69).)` | not applicable | desktop | build tool | Not specified by vcpkg | [source](https://ftpmirror.gnu.org/gnu/automake/automake-1.17.tar.gz) |
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

### Shader build tools (`x64-linux` only)

[ADR-0030](docs/adr/0030-desktop-graphics-audio-and-ui-renderer.md) compiles the desktop HLSL shaders with
SDL_shadercross, DXC and SPIRV-Cross, and requires reviewed inventory entries for the tools whose vcpkg metadata has
no licence before the shader build lands. The `sdl3-shadercross` port is a `host: true` dependency of the `desktop`
feature gated to `linux & x64`, because DXC has no macOS build; the committed SPIR-V and MSL outputs are what every
platform embeds. None of these tools is linked into or distributed with an Alumbra artefact. The inventory skips
platform-gated manifest dependencies on other triplets.

| Port | Pinned version | Terms | Record |
|------|----------------|-------|--------|
| `sdl3-shadercross` | `3.0.0-preview2` (SDL_shadercross `1ca46e0`) | `Zlib`, on the automatic list | No review needed. |
| `directx-dxc` | `2026-05-27` (DXC release `v1.9.2602.24`) | Metadata is `LicenseRef-vcpkg-null`. The installed copyright is DXC's `LICENSE.TXT`: the University of Illinois/NCSA licence, third-party notices, and GPL text for build and test dependencies only. | Reviewed host-tool exception in [`licence_policy.json`](tools/ci/licence_policy.json), keyed to the SHA-256 of the installed copyright, with the NCSA text recorded as `LicenseRef-DXC-LICENSE-TXT-NCSA`. The same rules as for `pkgconf` apply, and shipping a DXC file would void the classification (D-34). |
| `spirv-cross` | `1.4.363.0` | Metadata is `NOASSERTION`. The installed copyright is the Apache License, Version 2.0. | Reviewed exception resolving to `Apache-2.0`. |
| `spirv-headers` | `1.4.357.0` | Metadata is `NOASSERTION`. The installed copyright grants the Khronos MIT-style terms, the MIT licence for a bundled jsoncpp tool and Creative Commons Attribution 4.0 for Markdown documentation that the port does not install. | Reviewed exception resolving to `MIT`. |

The reviews are recorded for acceptance by the maintainer when the pull request that adds the shader build (#68) is
merged. `sdl3` now also appears with the role `target and build dependency` on `x64-linux`, because
`sdl3-shadercross` depends on it as a host tool.

### Linux audio backend (`x64-linux` only)

[ADR-0030](docs/adr/0030-desktop-graphics-audio-and-ui-renderer.md) asks #69 for a Linux audio backend for the
desktop `sdl3` port whose licence is recorded here. The manifest selects the port's `alsa` feature on Linux, which
adds the `alsa` port (alsa-lib). PipeWire and PulseAudio are not selected: both serve ALSA clients through their ALSA
plugins.

| Port | Pinned version | Terms | Record |
|------|----------------|-------|--------|
| `alsa` | `1.2.15.3#1` | `LGPL-2.1-or-later`, exercised under LGPL 2.1, on the conditional list | Reviewed exception in [`licence_policy.json`](tools/ci/licence_policy.json). The Linux triplets build `alsa` as a shared library, and SDL's ALSA driver opens `libasound.so.2` at run time (`SDL_ALSA_SHARED`), so no alsa-lib code is linked into an Alumbra artefact and the library stays separately replaceable under the desktop rule of [ADR-0014](docs/adr/0014-dependency-and-licensing-policy.md). Distributing a build that bundles `libasound` carries the LGPL 2.1 obligations of that ADR (D-34). |

The review is recorded for acceptance by the maintainer when the pull request that adds the audio sink (#69) is merged.
The port also adds the `vcpkg-make` build helper, whose vcpkg metadata has no licence (`LicenseRef-vcpkg-null`). Its
installed copyright states that the CMake scripts follow vcpkg's MIT terms and that the Automake shell scripts it copies
(`ar-lib`, `compile` and the wrappers) are GPL-2.0-or-later. It is a host-only build helper reached through the `alsa`
port, so it gets the same reviewed host-tool exception as `pkgconf`, keyed to the SHA-256 of the installed copyright
in [`licence_policy.json`](tools/ci/licence_policy.json). Shipping a `vcpkg-make` file would void it (D-34).

The vcpkg overlay ports in [`cmake/ports`](cmake/ports/README.md) change build options and dependency minimums, not
licences, so the inventory is unchanged by them.

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

## Desktop UI text and font

[ADR-0030](docs/adr/0030-desktop-graphics-audio-and-ui-renderer.md) draws the desktop UI's text with HarfBuzz and
FreeType and one bundled font. Both libraries are target dependencies of the `desktop` feature, installed with
`default-features` off.

### Reviewed exception: `harfbuzz` 14.6.0 (#329)

| Field | Record |
|-------|--------|
| Keyed to | Baseline `9a5dfef5d1a0438a7d24856a85bd60f309d355e9`; port `harfbuzz` 14.6.0 with no port features; triplets `arm64-osx` and `x64-linux`; owning feature `desktop`; target role; static linkage; expression `MIT AND MIT-Modern-Variant` |
| Terms | `MIT-Modern-Variant` is HarfBuzz's own permissive grant (its COPYING, the "Old MIT" licence). It is not on ADR-0014's automatic list, so the entry resolves the expression to `MIT` through the same `reviewed_exceptions` mechanism as `pkgconf`, and the policy check requires the installed copyright to contain the reviewed wording |
| Features | None. `freetype`, `glib`, `icu`, `graphite2`, `cairo`, `png` and `zlib` are off, so no GPL or LGPL component is pulled in; fonts are read with HarfBuzz's own OpenType functions |
| Notices | The copyright notice and both paragraphs of the grant must accompany every distributed binary. The text is in the installed `share/harfbuzz/copyright`; release packaging (D-34) must add it to the binary notices |
| Decision | Recorded for acceptance by the maintainer when the pull request that adds the desktop UI renderer (#98) is merged |

### Reviewed choice: `freetype` 2.14.3 (#330)

| Field | Record |
|-------|--------|
| Keyed to | The same baseline, triplets and linkage; no port features; expression `FTL OR GPL-2.0-or-later` |
| Choice | The FreeType License (FTL). The GPL branch is never used, and ADR-0014 denies it |
| Features | `brotli`, `bzip2`, `png` and `zlib` are off. bzip2 (`bzip2-1.0.6`) and libpng (`libpng-2.0`) are not on the automatic list and stay off unless reviewed; FreeType uses its internal DEFLATE code |
| Credit | The FTL requires this text in the documentation of every product that ships FreeType: "Portions of this software are copyright (c) <year> The FreeType Project (www.freetype.org). All rights reserved." Release packaging (D-34) must include it with the product's documentation |
| Windows | The port builds from source with the stock recipe on every desktop triplet, including `x64-windows` ([ADR-0031](docs/adr/0031-windows-support-scope-and-toolchain.md)). CI builds no Windows triplet yet, so this is recorded from the recipe and not from a build |

### Bundled font: Inter Regular 4.1

| Field | Record |
|-------|--------|
| Licence | SIL Open Font License 1.1, with no reserved font name. The font is used unmodified |
| Files | [`antumbra/desktop_ui/fonts/Inter-Regular.ttf`](antumbra/desktop_ui/fonts/Inter-Regular.ttf) (SHA-256 `40d692fce188e4471e2b3cba937be967878f631ad3ebbbdcd587687c7ebe0c82`) and its licence, [`OFL-Inter.txt`](antumbra/desktop_ui/fonts/OFL-Inter.txt), from the [Inter 4.1 release](https://github.com/rsms/inter/releases/tag/v4.1) |
| Use | Embedded in the desktop UI library at build time. Binary distributions must carry the OFL text and the copyright line, "Copyright (c) 2016 The Inter Project Authors" |

OFL-1.1 is conditional in [`licence_policy.json`](tools/ci/licence_policy.json) for fonts and needs a review. This
entry is that review for the one bundled font; the vcpkg inventory does not see it because it is a vendored file.

## Text algorithm review

The core text dependency versions and build options are pinned by the vcpkg baseline in `vcpkg.json`. SheenBidi 3.0.0 is Apache-2.0 and libunibreak 7.0 is `zlib-acknowledgement`; the latter has a version-specific ADR-0014 review in `tools/ci/licence_policy.json` because that SPDX identifier is outside the automatic list. Retain each upstream copyright and licence notice. For libunibreak, mark altered source versions. SheenBidi's vcpkg port configures with `SB_CONFIG_UNITY=ON` and leaves the experimental `text-api` feature disabled. The libunibreak port applies `fix_export.patch` and passes no additional CMake configure options. Both use the default static triplet linkage.

The pinned source and licence evidence is [SheenBidi v3.0.0](https://github.com/Tehreer/SheenBidi/tree/v3.0.0), [its Apache-2.0 licence](https://github.com/Tehreer/SheenBidi/blob/v3.0.0/LICENSE), [libunibreak 7.0](https://github.com/adah1972/libunibreak/tree/libunibreak_7_0) and [its licence file](https://github.com/adah1972/libunibreak/blob/libunibreak_7_0/LICENCE). The vcpkg ports are available on the declared target triplets; the local validation for this change built both on arm64-osx. Windows x64 and wasm32-emscripten require their respective CI toolchains for build confirmation.

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

## Documentation site tooling

Build-time tools only: nothing below is linked into, shipped with or loaded by an Alumbra artefact. The site
project in [`tools/site/`](tools/site/) pins its direct dependencies in `package.json` and its full closure,
including integrity hashes, in `pnpm-lock.yaml`.

| Tool | Version | SPDX licence | Use |
|------|---------|--------------|-----|
| Astro | 7.4.4 | MIT | Static site generator |
| Starlight (`@astrojs/starlight`) | 0.42.8 | MIT | Documentation theme, navigation and Pagefind search |
| `@astrojs/markdown-remark` | 7.3.5 | MIT | Remark and rehype processing |
| rehype-mermaid | 3.0.0 | MIT | Renders Mermaid diagrams to SVG at build time |
| Playwright | 1.64.1 | Apache-2.0 | Headless Chromium for rehype-mermaid; the browser is downloaded at build time |
| pnpm | 12.16.1 | MIT | Package manager |
| lychee | 0.24.2 | Apache-2.0 OR MIT | Link checker |
| typos | 1.51.1 | Apache-2.0 OR MIT | Spell checker |
| Vale | 3.25.1 | MIT | Prose linter |
| Vale Google style | 0.7.1 | MIT | Vale rule package |

The npm closure (455 packages) is MIT, ISC, BSD-2-Clause, BSD-3-Clause, Apache-2.0, BlueOak-1.0.0, CC0-1.0,
Unlicense and Python-2.0, plus MPL-2.0 (`lightningcss`, used only by Vite while building), `(MPL-2.0 OR
Apache-2.0)` (`dompurify`) and `(CC-BY-4.0 AND OFL-1.1 AND MIT)` (`@fortawesome/fontawesome-free`, whose icon font
Mermaid's headless renderer loads). Sharp and its LGPL-3.0 libvips binaries are removed with a pnpm override because
the site uses no image optimisation. Because none of this is distributed, ADR-0014's distribution conditions do not
apply; the closure's licences are recorded here rather than gated by the vcpkg inventory check.
