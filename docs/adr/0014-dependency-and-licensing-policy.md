# 0014 — Allow permissive dependencies and constrain weak copyleft by platform

- **Status:** Accepted
- **Date:** 2026-10-31
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #32

## Context

[ADR-0010](0010-license-and-visibility.md) establishes a public Apache-2.0 project and excludes GPL
dependencies. D-32 makes its dependency policy enforceable before #38 introduces FFmpeg, #40 inventories
dependencies and #85 implements desktop decoders. Copyright permission, codec patents and distribution
terms are separate concerns. A shared-library filename alone does not establish LGPL compliance.

This decision settles licence eligibility, platform conditions, the review process and FFmpeg's build
boundary. D-14 chooses protocol implementations, D-15 chooses text rendering and D-34 defines release
packaging. Their detailed implementation choices remain open.

## Decision drivers

- Preserve desktop playback breadth without imposing copyleft on Alumbra's portable core.
- Keep Apple and web artefacts free of LGPL; make remaining distributor obligations explicit.
- Review reproducible dependency graphs and fail closed on unknown or changed licence conditions.

## Considered options

1. **A — Permissive-only:** simplest compliance, but loses FFmpeg breadth and some protocol/text libraries.
2. **B — Permissive plus constrained weak copyleft:** retains desktop breadth with platform-specific reviews.
3. **C — Weak copyleft everywhere:** pursues parity but transfers disputed relinking and store obligations
   to every embedder.

## Decision

Chosen option: **B, with A's discipline wherever it costs little.** Prefer platform decoders and permissive
dependencies; eligibility is not adoption. Merging this ADR accepts the decision.

### SPDX eligibility

| Class | SPDX identifiers | Rule |
|-------|------------------|------|
| Automatic | `Apache-2.0`, `MIT`, `BSD-2-Clause`, `BSD-3-Clause`, `Zlib`, `ISC`, `curl`, `FTL`, `BSL-1.0` | Retain licence, attribution and applicable NOTICE text; still record every dependency. |
| Fonts only | `OFL-1.1` | Retain font notices and observe reserved-font-name conditions. |
| Conditional | `MPL-2.0`, `LGPL-2.1-only`, `LGPL-2.1-or-later` | Recorded ADR-level review and the conditions below; use LGPL 2.1 terms, not an upgrade to LGPL 3. |
| Denied | GPL, AGPL, LGPL 3 variants, any non-free licence | No exception-file waiver. LGPL 3 requires a new ADR, including installation-information implications for TV devices. |
| Unresolved | Missing licence, `NOASSERTION`, `LicenseRef-vcpkg-null`, unlisted identifiers or exceptions | Block until a recorded review establishes permitted terms; never interpret unknown as permissive. |

Evaluate SPDX **expressions**, not substring matches. Every `AND` obligation must be satisfied; an `OR`
requires a recorded choice of an eligible branch (for example FreeType under FTL, not GPL). `WITH`
exceptions require review. Check transitive dependencies and compatibility of the combined build too:
an individually permissive licence does not automatically make its combination with LGPL 2.1 acceptable.

### Linkage and distribution

| Target | LGPL policy |
|--------|-------------|
| Desktop | Separately replaceable shared libraries only, with a reviewed distribution/replacement procedure. FFmpeg is a desktop fallback and breadth layer. |
| Android / Google TV | Same conditions; `.so` files remain separate in the AAR/APK. A review must demonstrate replacement and operation with an interface-compatible modified library, accounting for APK signing, repackaging and device restrictions. MediaCodec is the default; no Android LGPL dependency is planned. Each future one needs its own review entry. |
| iOS / iPadOS / tvOS | No LGPL code in any Alumbra artefact, not merely App Store builds. |
| Web | No LGPL code in the WASM bundle or a shipped side module. Browser decoders remain the platform path. |

LGPL distributions must retain notices and licence texts, provide complete corresponding library source
including modifications and build/installation scripts, and preserve LGPL 2.1 section 6 rights to modify
the work for personal use and reverse engineer it to debug those modifications. The shared-library
mechanism must actually operate with a user-installed, interface-compatible replacement. Document the
relinking/replacement materials and procedure required by the selected section 6 route; reject a
distribution if these rights cannot be exercised. An SDK does not discharge an embedding app's obligations.
Provide the corresponding source alongside binaries, or equivalent access from the same distribution
location; a generic upstream link or source-only offer is not a substitute.

The Apple/web exclusions are absolute **project policy**, not a legal guarantee about store acceptance,
patents or other licences.

MPL-2.0 permits static or dynamic linkage on any platform after review. For libsrt, keep covered files
unmodified, preserve notices and provide a working pointer to the exact corresponding MPL source.
Modifying covered files requires a new review and publication of those files under MPL; Alumbra's separate
files remain Apache-2.0.

### FFmpeg

Approve **LGPL-2.1-or-later, exercised under LGPL 2.1**, shared libraries only. Never enable `gpl`,
`version3` or `nonfree`, including indirectly through vcpkg features. No GPL or non-free external library
may enter the build.

The distributable build has this mandatory configure baseline, plus pinned toolchain flags and the
reviewed component enable-list:

```text
--disable-gpl --disable-version3 --disable-nonfree
--enable-shared --disable-static
--disable-autodetect --disable-programs --disable-doc
--disable-network --disable-protocols --disable-everything
--disable-avdevice --disable-avfilter --disable-swscale
```

Libraries are **avcodec, avformat, avutil and swresample**; swscale requires a documented need and review.
Enable only the decoders, demuxers, parsers and necessary bitstream filters owned by the consuming tasks
(#85 owns decoders and uses native FFmpeg decoders only). Encoders, muxers, devices and filters stay
disabled. The complete configure command and resulting component lists must be recorded, not inferred
from this baseline.

`--disable-network` is final, independent of D-08's transport decisions. No built-in protocol may be
re-enabled. Alumbra's sources (ADR-0011, D-08 and D-14) provide bytes and seeks through a custom
`AVIOContext`; FFmpeg must not open files, URLs or sockets itself.

**Staging:** #38 may use the stock vcpkg port for local development, with default features disabled and
only `avcodec`, `avformat` and `swresample` selected (`avutil` is unconditional). The overlay triplet sets
FFmpeg linkage to dynamic. Tests check actual shared libraries and require `avcodec_license()`,
`avformat_license()` and `avutil_license()` to report LGPL 2.1-or-later. These tests do not prove the
network/component restrictions or complete legal compliance.

The inspected port already disables autodetection and documentation, omits unselected programs, and
chooses the installed copyright file from configure's reported licence. Its manifest declares
**`"license": null`** (an explicit null, not an omitted field; none of its features declares a licence) and
it has no supported extra-configure hook: it invokes its own `build.sh.in`, not
`vcpkg_configure_make`. `VCPKG_CONFIGURE_MAKE_OPTIONS` therefore does not narrow this port. At this
baseline, narrowing requires an **overlay port**; use a supported upstream hook instead only if a later
pinned port supplies one. The narrowed build, configuration/component assertions and corresponding
source package must land through #85 and D-34 **before any FFmpeg binary is distributed**. The #38
development configuration is not a release exception.

Verified against FFmpeg 9.0.2 and current upstream configure on 2026-10-31:

| Gate | Components / qualification |
|------|----------------------------|
| `version3` | `gmp`, `libaribb24`, `liblensfun`, `libopencore_amrnb`, `libopencore_amrwb`, `libvo_amrwbenc`, `mbedtls`, `rkmpp`; current master also lists `libastcenc`. `libsmbclient` requires GPL and version3. |
| `nonfree` | `cuda_nvcc`, `cuda_sdk`; the external nonfree list contains `decklink`, `libfdk_aac`, `libmpeghdec`, with master adding `libfdk_xhe_aac`. Current configure gates that external list when GPL is enabled; this policy excludes it regardless. |

OpenSSL 3 is not on the nonfree list; its GPL combination requires version3. None of these observations
authorizes an external integration: re-check configure, upstream licences and enabled features at each
baseline update.

### Other candidate libraries and patents

| Candidate | Decision boundary |
|-----------|-------------------|
| libass / FriBidi | libass is ISC but upstream's Autotools and Meson builds require FriBidi; there is no supported no-FriBidi switch. FriBidi is LGPL-2.1-or-later and eligible as a desktop shared library after version-specific review. Its current vcpkg expression also includes `Unicode-3.0`, outside the automatic list, which must be reviewed. D-15 must choose a non-LGPL path for Apple/web; ASS parity there is unresolved, not promised. |
| libsrt / librtmp / LIVE555 | D-14 selects libraries versus in-house protocols. libsrt is MPL-2.0 under the conditions above. librtmp is LGPL-2.1-or-later and eligible only under the desktop/Android conditions; its port also lacks licence metadata and needs review. **Current LIVE555 is LGPL-3.0-or-later**, not LGPL 2.1, and is excluded unless a new ADR changes that policy. |
| OpenH264 | Not adopted: platform decoders plus desktop FFmpeg cover H.264. Future adoption requires review of Cisco's binary terms, including separate end-user download, user controls and notices; compiling or prebundling the BSD source does not inherit Cisco's patent coverage. |

Required disclaimer: **"Alumbra's Apache-2.0 licence does not grant rights to third-party codec patents.
Distributors are responsible for determining and obtaining any patent licences required for their use
and distribution."** This does not negate Apache-2.0's contributor patent grant.

#85 defines a build option, its name and the enumerated decoder set that removes patent-pool-covered
software decoders (H.264, HEVC, AAC and similar) from FFmpeg, not merely from runtime selection.
Documentation must state the resulting feature loss. This option is not a patent-clearance guarantee.

### Review, inventory and enforcement

| Control | Required record / enforcement |
|---------|-------------------------------|
| Inventory | #40 generates `THIRD_PARTY.md` for direct and transitive dependencies: name, version, SPDX expression and selected terms, purpose, linkage, platforms, owning feature, upstream/source URL and approval reference. |
| Review | Conditional/unknown entries record exact version, features, target/linkage, source/configuration evidence, obligations, replacement/source-delivery procedure and accepted ADR reference. FFmpeg's policy approval is this ADR; #40 records the pinned build evidence. Later library adoption is reviewed in its owning definition. |
| Pinning | Full-SHA vcpkg `builtin-baseline`; `overrides` only with recorded rationale. Pin registry/tool/overlay inputs and use ecosystem lockfiles when npm, Gradle and SwiftPM arrive. Renovate/Dependabot updates re-run review on licence, feature or distribution-condition changes. |
| CI | #40 implements a deterministic SPDX-expression allowlist check over per-port SPDX documents and copyright files, with reviewed exceptions keyed to the build inputs. Fail on denied terms, unreviewed unknowns, invalid linkage/platforms and stale inventory; name the dependency and rule. Review cannot waive a denied licence. |
| SPDX inputs | vcpkg emits **SPDX 2.3 JSON**. Use its effective package `licenseConcluded`, not only `licenseDeclared`, which the inspected generator sets to `NOASSERTION`; file-level unknowns are not automatic package failures. The generator distinguishes two unknown package licences: a `license` field omitted from the port and every selected feature yields `NOASSERTION` (for example librtmp), while an explicit `"license": null` yields `LicenseRef-vcpkg-null`, joined by `AND` to any licences that selected features declare (FFmpeg yields exactly `LicenseRef-vcpkg-null`). The check and reviewed exceptions must match the value actually produced. Both require source/copyright/configuration evidence and a reviewed entry, never guessed terms. |
| SBOM | D-34 assembles **SPDX 2.3 JSON per release artefact**, attached to GitHub Releases, covering that artefact's complete shipped graph, versions, licence choices, relationships and build identity. Per-port vcpkg documents are inputs, not a complete release SBOM. Version 2.3 matches the verified producer; a producer/schema change requires review. |
| Notices | Each SDK includes applicable third-party licence/NOTICE text and source pointers. FFmpeg binaries ship with exact corresponding source, patches, configure flags and build scripts, available with the binary download; provide embedder instructions for notices and replacement. |

Do not adopt ScanCode or the OSS Review Toolkit now. Revisit graph/scanner coverage when npm, Gradle and
SwiftPM dependencies arrive. A deterministic metadata check enforces recorded decisions; it does not
discover all licence obligations or replace legal review. A REUSE compliance check using repository-level
metadata (for example `REUSE.toml` or sidecar `.license` files) for Alumbra-owned files is a follow-up, not
implemented by this ADR. It must not require comments or headers in Alumbra-owned source; legally required
upstream notices remain intact.

## Consequences

**Positive**
- Desktop keeps FFmpeg breadth; Apple/web avoid LGPL obligations by policy.
- Reproducible inputs, explicit reviews and per-artefact notices/SBOMs make dependency changes auditable.

**Negative / costs**
- Dynamic packaging, actual replacement support and corresponding-source distribution burden embedders.
- Narrow FFmpeg builds require overlay maintenance at the inspected baseline.
- Protocol/text feature parity is not guaranteed; unknown metadata requires manual evidence.

**Follow-ups**
- **#38:** pinned manifest, minimal stock FFmpeg development features, dynamic triplets and LGPL tests.
- **#40:** inventory, reviewed exceptions, SPDX-expression policy check and notices.
- **#85:** narrowed FFmpeg build and decoder/component assertions, custom-I/O boundary and patent-decoder
  removal option; no distribution before the narrowed build is ready.
- **D-14:** protocol choices, including libsrt/librtmp reviews; current LIVE555 remains excluded.
- **D-15:** desktop FriBidi/Unicode-data review and a non-LGPL Apple/web text path; resolve ASS support.
- **D-34:** release-specific SPDX 2.3 SBOMs, source/notices packaging and distribution compliance gates.
- **REUSE metadata check:** create a follow-up task for repository-level metadata (for example
  `REUSE.toml` or sidecar `.license` files), without comments or headers in Alumbra-owned source.
  Preserve legally required upstream notices.

## References

Primary sources checked on 2026-10-31; snapshots below establish facts, not the future #38 baseline.

- [D-32 discussion](https://github.com/LuisAhumadaMartens/alumbra/issues/32),
  [ADR-0010](0010-license-and-visibility.md), [ADR-0011](0011-sources-open-network-stream.md).
- [FFmpeg legal checklist](https://ffmpeg.org/legal.html),
  [LGPL 2.1 sections 4 and 6](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html).
- FFmpeg [9.0.2 configure](https://github.com/FFmpeg/FFmpeg/blob/n9.0.2/configure),
  [current configure snapshot](https://github.com/FFmpeg/FFmpeg/blob/6d1bfd760916ab52814b950692d1c1635d29f3c7/configure)
  and [licensing notes](https://github.com/FFmpeg/FFmpeg/blob/n9.0.2/LICENSE.md).
- vcpkg at `9a5dfef5d1a0438a7d24856a85bd60f309d355e9`: FFmpeg
  [manifest](https://github.com/microsoft/vcpkg/blob/9a5dfef5d1a0438a7d24856a85bd60f309d355e9/ports/ffmpeg/vcpkg.json),
  [portfile](https://github.com/microsoft/vcpkg/blob/9a5dfef5d1a0438a7d24856a85bd60f309d355e9/ports/ffmpeg/portfile.cmake)
  and [build script](https://github.com/microsoft/vcpkg/blob/9a5dfef5d1a0438a7d24856a85bd60f309d355e9/ports/ffmpeg/build.sh.in);
  [triplet variables and per-port customization](https://learn.microsoft.com/en-us/vcpkg/users/triplets),
  [versioning](https://learn.microsoft.com/en-us/vcpkg/users/versioning).
- vcpkg-tool [SPDX generator](https://github.com/microsoft/vcpkg-tool/blob/3018eed59e56cb116c6e34a622775298df37cba8/src/vcpkg/spdx.cpp)
  and [SPDX version constant](https://github.com/microsoft/vcpkg-tool/blob/3018eed59e56cb116c6e34a622775298df37cba8/include/vcpkg/base/contractual-constants.h);
  [SPDX specifications](https://spdx.dev/use/specifications/), [REUSE specification](https://reuse.software/spec/).
- [LIVE555 official 2026-10-29 source archive](https://download.live555.com/live.2026.10.29.tar.gz)
  (`liveMedia/include/liveMedia.hh` licence header),
  [upstream FAQ](http://www.live555.com/liveMedia/faq.html#copyright-and-license) and
  [vcpkg licence declaration](https://github.com/microsoft/vcpkg/blob/9a5dfef5d1a0438a7d24856a85bd60f309d355e9/ports/live555/vcpkg.json).
- [librtmp upstream](https://rtmpdump.mplayerhq.hu/), source
  [licence header at vcpkg's selected revision](https://github.com/mirror/rtmpdump/blob/6f6bb1353fc84f4cc37138baa99f586750028a01/librtmp/rtmp.c)
  and [portfile selecting it](https://github.com/microsoft/vcpkg/blob/9a5dfef5d1a0438a7d24856a85bd60f309d355e9/ports/librtmp/portfile.cmake).
- [FriBidi licence header](https://github.com/fribidi/fribidi/blob/master/lib/fribidi.h) and
  [vcpkg licence expression](https://github.com/microsoft/vcpkg/blob/9a5dfef5d1a0438a7d24856a85bd60f309d355e9/ports/fribidi/vcpkg.json);
  libass [ISC licence](https://github.com/libass/libass/blob/00fe2ec7ae677b7568fbbcbcbcb67d34bb4292a3/COPYING),
  [Autotools dependency](https://github.com/libass/libass/blob/00fe2ec7ae677b7568fbbcbcbcb67d34bb4292a3/configure.ac)
  and [Meson dependency](https://github.com/libass/libass/blob/00fe2ec7ae677b7568fbbcbcbcb67d34bb4292a3/meson.build).
- [libsrt licence](https://github.com/Haivision/srt/blob/master/LICENSE),
  [Mozilla MPL FAQ](https://www.mozilla.org/en-US/MPL/2.0/FAQ/),
  [Cisco OpenH264 binary terms](https://www.openh264.org/BINARY_LICENSE.txt).
