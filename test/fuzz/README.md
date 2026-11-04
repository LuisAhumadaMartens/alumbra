# Fuzz targets

Every parser of untrusted input has a libFuzzer target here
([ADR-0021](../../docs/adr/0021-testing-strategy.md#fuzzing-sanitizers-and-failure-handling)). One helper
registers a target; it then builds as a corpus-replay test on every compiler and, in fuzzing builds, as a
libFuzzer executable that CI runs with a fixed budget on every C++ pull request.

| Target | Exercises |
|--------|-----------|
| `byte_reader_fuzzer` | [`ByteReader` and `BitReader`](../../docs/architecture/byte-readers.md): every read, skip and Exp-Golomb decode, checked against a bit-by-bit reference model, including error codes and the unchanged cursor after a failure |
| `codec_config_fuzzer` | Every [codec configuration parser](../../docs/architecture/codec-config.md): `avcC`, SPS, `hvcC`, VPS, `vpcC`, `av1C`, AV1 sequence headers, `AudioSpecificConfig`, `OpusHead`, `dOps`, `dac3`, `dec3`, FLAC, Annex B conversion, emulation-prevention removal and codec strings, checked for round trips and for the error codes |
| `mp4_demuxer_fuzzer` | The [ISO-BMFF demuxer](../../docs/architecture/demuxers.md): progressive and fragmented MP4 files run through `Mp4Demuxer` to the end, followed by seek operations decoded from the fuzz input |
| `fmp4_fragment_fuzzer` | Fragmented MP4 boxes, samples and seek-map operations through the full read loop |
| `ts_demuxer_fuzzer` | MPEG-TS streams run through the full demuxer read loop, followed by seek operations decoded from the fuzz input |
| `ts_psi_fuzzer` | MPEG-TS packet framing, section assembly, PAT and PMT parsing, CRC handling and PES headers |
| `mkv_demuxer_fuzzer` | WebM and Matroska files run through the full demuxer read loop, followed by seek operations decoded from the fuzz input |
| `ebml_fuzzer` | EBML variable-length integers, element headers and values, plus Matroska header, seek-head, info, tracks, cues and block-group handlers |
| `error_detail_fuzzer` | The bounded `ErrorDetail` payload decoder and the shared URL and text redactor ([errors](../../docs/architecture/errors.md)) |
| `manifest_fuzzer` | The [presentation model](../../docs/architecture/presentation-model.md) validator and builders: presentations built from bytes are validated, then checked for segment lookup invariants and refreshed, and the segment-template parser and the id encoder run on raw bytes |
| `dash_mpd_fuzzer` | The bounded DASH MPD XML parser, including nested Period, SegmentTemplate, SegmentList and SegmentBase data |
| `dash_duration_fuzzer` | DASH `xs:duration` and `xs:dateTime` attributes, including calendar components and fractional seconds |
| `dash_template_fuzzer` | DASH identifier expansion for `$Number$`, `$Time$`, `$Bandwidth$`, `$RepresentationID$` and escaped dollar tokens |
| `dash_mpd_update_fuzzer` | Two DASH MPDs parsed and merged as a presentation refresh, including live SegmentTimeline windows |
| `vast_parser_fuzzer` | Bounded VAST XML parsing and mock-fetch wrapper resolution, including malformed and repeated wrapper responses |
| `vmap_parser_fuzzer` | VMAP parsing, VAST-as-preroll detection and schedule construction |
| `vast_macro_fuzzer` | VAST macro expansion with known, unknown, repeated and malformed tokens |
| `settings_value_parser_fuzzer` | The settings `key=value` parser and type-aware value parser ([settings runtime](../../docs/architecture/settings.md)): every outcome is `InvalidArgument` or a value that satisfies its metadata and round-trips through its text form |
| `ass_parser_fuzzer` | The bounded ASS/SSA script, Matroska event-line glue and dialogue parser ([ASS subset](../../docs/architecture/ass-subset-renderer.md)): arbitrary script bytes produce only bounded cues with valid time intervals |

| `url_parser_fuzzer` | The RFC 3986 [URL parser and reference resolver](../../docs/architecture/data-sources.md): parse/print stability, dot-segment removal and resolution of a base and a reference separated by a newline |
| `source_sniffer_fuzzer` | The [source-type sniffer](../../docs/architecture/data-sources.md#source-type-detection): the 4 KiB to 64 KiB budget, the need-more-bytes contract and that extension and `Content-Type` never override a signature |
| `ttml_parser_fuzzer` | The bounded TTML parser: arbitrary XML bytes, secure rejection of DTD and entity declarations, depth limits and cue-count limits |
| `abr_trace_fuzzer` | CSV throughput and Mahimahi packet-delivery trace parsing for the [ABR simulator](../../docs/architecture/abr-simulation.md) |
| `srt_parser_fuzzer` | The untrusted SubRip parser: incremental bytes, UTF-8 and UTF-16 byte order marks, fallback encoding conversion, cue timing order and valid cue intervals |
| `container_probe_fuzzer` | The [source-type sniffer](../../docs/architecture/data-sources.md#source-type-detection): the 4 KiB to 64 KiB budget, the need-more-bytes contract and that extension and `Content-Type` never override a signature |
| `webvtt_fuzzer` | The untrusted WebVTT file and incremental parsers, `wvtt` samples, timestamp-map metadata, cue timing and text sanitisation |
| `ffmpeg_decoder_fuzzer` | One bounded arbitrary H.264 or HEVC sample through the desktop `FfmpegDecoder` wrapper |
| `hls_playlist_fuzzer` | HLS media and multivariant playlist parsing |
| `hls_attribute_list_fuzzer` | HLS attribute-list quoting, delimiters and typed values |
| `hls_variable_substitution_fuzzer` | HLS query parameter, imported and local variable substitution |
| `hls_packed_audio_id3_fuzzer` | Packed MPEG audio framing and ID3 metadata emitted by HLS audio playlists |
| `hls_aes128_decrypt_fuzzer` | HLS AES-128 key declarations, IV parsing and encrypted playlist metadata |
| `hls_playlist_update_fuzzer` | HLS playlist refresh and live-window segment merging |

## Layout

```text
test/fuzz/
  CMakeLists.txt                 registrations
  fuzz_target.h                  declares LLVMFuzzerTestOneInput
  replay_main.cc                 standalone replay driver
  <target>/
    <target>.cc                  the target
    <target>.dict                optional libFuzzer dictionary
    corpus/                      seed inputs
    regressions/                 minimized reproducers of fixed findings and boundary cases
  canary/                        deliberately failing targets (CI only)
```

Name targets in lower snake case ending in `_fuzzer`. Corpus files are small binary files committed with
the target; give hand-written seeds descriptive names. Never commit real stream URLs, credentials or
licence data.

Regenerate the demux seeds with `python3 test/fuzz/generate_demux_corpora.py`. The script builds small
PAT, PMT and EBML examples directly and wraps generated MP4, MPEG-TS and WebM fixtures. Wrapped
demux inputs contain a selector, the `AL82` marker, a little-endian file-byte count, file bytes and
seek-operation bytes. The fuzz input provider separates those byte ranges; each seek byte selects a
time in the parsed seek map.

The HLS playlist seeds are small, synthetic reductions of Apple HLS example playlist structures. They retain
multivariant, audio-group, variable, encryption, date-range and sliding-window syntax without copying stream URLs
or media. The source examples are linked from [Apple's HLS streaming examples](https://developer.apple.com/streaming/examples/)
and the [Apple HLS authoring specification](https://developer.apple.com/streaming/hls-authoring-specification-for-apple-devices.html).

## Registering a target

```cmake
alumbra_add_fuzz_target(
  NAME byte_reader_fuzzer
  SOURCES byte_reader_fuzzer/byte_reader_fuzzer.cc
  CORPUS byte_reader_fuzzer/corpus byte_reader_fuzzer/regressions
  DICT byte_reader_fuzzer/byte_reader_fuzzer.dict
  LIBRARIES umbra::base)
```

`alumbra_add_fuzz_target` is defined in [`cmake/AlumbraFuzz.cmake`](../../cmake/AlumbraFuzz.cmake) and
is available wherever tests are built. Relative paths resolve against the calling `CMakeLists.txt`.

| Argument | Meaning |
|----------|---------|
| `NAME` | Target name, `<something>_fuzzer`. Required. |
| `SOURCES` | Sources that define `LLVMFuzzerTestOneInput`. Required. |
| `CORPUS` | One or more existing directories of inputs, read recursively: normally `corpus` and, once it has inputs, `regressions`. Required; together they must contain at least one file. |
| `DICT` | A libFuzzer dictionary file. Optional. |
| `LIBRARIES` | Libraries the target links, such as `umbra::base`. Optional. |

The helper always creates:

- `<NAME>_replay`, the sources linked with [`replay_main.cc`](replay_main.cc), and the CTest test
  `fuzz_replay.<NAME>` (label `fuzz_replay`). The driver collects every regular file under the corpus
  directories, sorts them, copies each into an exactly sized buffer (a non-null pointer even for an
  empty input, as libFuzzer passes) and calls the target once per input.
  It fails if it finds no inputs or cannot read one; a crash, sanitizer report or `std::abort` fails the
  test. It needs no libFuzzer, so corpora replay in `ctest` on every preset and every CI leg: GCC, Clang
  and Xcode, debug and release, and the ASan/UBSan and TSan builds.

With `ALUMBRA_FUZZING=ON` it also creates:

- `<NAME>`, the libFuzzer executable (`-fsanitize=fuzzer,address,undefined`), and the CTest test
  `fuzz_smoke.<NAME>` (label `fuzz_smoke`), which runs it with the smoke budget below.

Both are added to the `alumbra_fuzz` build target. The sources are compiled with Alumbra's usual target
options, so warnings, sanitizers and clang-tidy apply to them as to any test.

## Writing a target

```cpp
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

#include "absl/status/statusor.h"
#include "fuzz_target.h"
#include "umbra/base/byte_reader.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  umbra::base::ByteReader reader(std::span<const std::uint8_t>(data, size));
  const absl::StatusOr<std::uint32_t> length = reader.ReadU32();
  if (length.ok() != (size >= 4) || reader.position() != (size >= 4 ? 4 : 0)) {
    std::abort();
  }
  return 0;
}
```

A target must accept any input, including an empty one, and be deterministic: no threads, clocks, real
sleeps, network, files or global state that survives a call. Check the parser's invariants and its
results against a simpler model where one exists, and call `std::abort()` when they are violated, so the
fuzzer finds logic errors as well as memory errors. Return 0.

## Fuzzing builds

The `fuzz` preset builds with upstream Clang, ASan and UBSan (`-fno-sanitize-recover=undefined`), libFuzzer
coverage instrumentation on every Alumbra target, and `ALUMBRA_PORT=none`, because no current target needs
the desktop port. Its build preset builds only `alumbra_fuzz` and its test preset runs only the fuzz labels.
Configuration fails unless the compiler is upstream Clang that can link `-fsanitize=fuzzer`. Apple Clang
and GCC do not ship libFuzzer.

On Linux, use the LLVM major in [`tools/ci/llvm-major`](../../tools/ci/llvm-major) and its compiler-rt
package (`libclang-rt-<major>-dev` on Ubuntu) and select the instrumented dependency triplet, as CI does:

```sh
export CC=clang-21 CXX=clang++-21
cmake --preset fuzz -DVCPKG_TARGET_TRIPLET=x64-linux-asan
cmake --build --preset fuzz
ctest --preset fuzz
```

`cmake --workflow --preset fuzz` runs the same three steps with the host's default triplet, whose
dependencies are not instrumented.

On macOS, Homebrew LLVM's `clang` and `clang++` (from `brew --prefix llvm`) work as `CC` and `CXX`
only when its libc++ headers and sanitizer runtimes support the host. With LLVM 21.1.8 on macOS 27, its
libc++ does not compile against the macOS 27.2 SDK, and with the 26.5 SDK its ASan runtime hangs while
starting, so fuzz on Linux instead, for example in an Ubuntu 26.04 container. CI runs fuzzing builds only
on Linux; the macOS legs still replay every corpus.

## Smoke budget

`fuzz_smoke.<NAME>` runs [`tools/ci/run_fuzz_target.py`](../../tools/ci/run_fuzz_target.py), which starts
the target with a fixed budget, a fresh writable corpus directory under `build/fuzz/fuzz/<NAME>/`, and the
registered corpus directories after it, which libFuzzer only reads:

```text
<NAME> -runs=100000 -seed=1 -rss_limit_mb=2048 -timeout=10 -print_final_stats=1
       -artifact_prefix=<reports>/<NAME>/ [-dict=<DICT>] <work corpus> <CORPUS>...
```

| Cache variable | Flag | Value | Why |
|----------------|------|-------|-----|
| `ALUMBRA_FUZZ_RUNS` | `-runs` | 100000 | Each target adds hundreds of new corpus units beyond its seeds, and the whole smoke, canaries included, takes under two minutes on the CI runner (measurements below). |
| `ALUMBRA_FUZZ_SEED` | `-seed` | 1 | A fixed seed makes a pull request's run reproducible from its commit. libFuzzer treats 0 as "random", so the seed must be positive. |
| `ALUMBRA_FUZZ_RSS_LIMIT_MB` | `-rss_limit_mb` | 2048 | About four times the measured peak of about 500 MB, which ASan's shadow memory and quarantine dominate, and an eighth of the runner's 16 GB. |
| `ALUMBRA_FUZZ_TIMEOUT` | `-timeout` | 10 | No measured input takes a second; ten seconds flags a hang with a wide margin for a shared runner and keeps the timeout canary short. |

Measured with `-print_final_stats=1` and the budget above; every CI run reports the same statistics in
its job summary and `fuzz-reports` artefact. The CI runner is GitHub's `ubuntu-26.04` image 20261026.180.1
(4 x64 CPUs, 16 GB) with Clang 21.1.8 and `x64-linux-asan` dependencies; the container used the host's
uninstrumented `arm64-linux` dependencies.

| Target | Host | Seconds | exec/s | New units | Slowest unit (s) | Peak RSS (MB) |
|--------|------|---------|--------|-----------|------------------|---------------|
| `byte_reader_fuzzer` | CI runner | 19.5 | 5263 | 701 | 0 | 486 |
| `error_detail_fuzzer` | CI runner | 68.6 | 1470 | 914 | 0 | 496 |
| `byte_reader_fuzzer` | Ubuntu 26.04 container, 4 arm64 CPUs | 7.8 | 14285 | 723 | 0 | 481 |
| `error_detail_fuzzer` | Ubuntu 26.04 container, 4 arm64 CPUs | 26.9 | 3846 | 626 | 0 | 504 |

The timeout canary adds about 12 seconds, because libFuzzer checks the timeout every few seconds; the
other canaries finish in under a second.

The runner fails the test when libFuzzer reports a crash (including any ASan or UBSan error), a leak,
running out of memory (`-malloc_limit_mb` bounds a single allocation and `-rss_limit_mb` bounds total RSS) or a
timeout; when it exits with an error it does not recognise; when the corpus is missing or empty; or when it
finishes without executing the whole `-runs` budget. Change a budget only with new measurements, and
record them here.

Each run writes `libfuzzer.log`, `result.json` (budget, statistics, finding and reproducers) and any
`crash-`, `leak-`, `oom-` or `timeout-` reproducer to `$ALUMBRA_FUZZ_REPORT_DIR/<NAME>/`, or to
`build/fuzz/fuzz/reports/<NAME>/` when the variable is unset. On failure it prints the log, the
reproducer and the command that replays it. CI sets the variable, summarises every `result.json` in the
job summary with [`tools/ci/summarize_fuzz.py`](../../tools/ci/summarize_fuzz.py) and uploads the whole
directory as the `fuzz-reports` artefact, whether or not the job passed.

The Linux fuzz job also combines source profiles from the six container targets. Its job summary shows
the file-level coverage table; the `fuzz-reports` artefact contains `demux-coverage.txt`, with detailed
line and function counts for review of box, EBML element and transport table handlers. The same artefact
contains separate line and function reports for each available text target under `text-coverage/`.

## Canaries

`ALUMBRA_FUZZ_CANARIES=ON` (fuzzing builds only) adds four deliberately broken targets in
[`canary/`](canary/), each registered as `fuzz_canary.<kind>` (label `fuzz_canary`) and run by the same
runner with the same budget and seed corpus:

| Test | Seeded bug | Proves |
|------|------------|--------|
| `fuzz_canary.crash` | Reads one byte past an input that starts with `FUZZ`; the seed is `FUZz`, so libFuzzer must mutate it (seed 1 finds it after 1286 runs) | Mutation, ASan, and a `crash-` reproducer |
| `fuzz_canary.leak` | Leaks an allocation for every non-empty input | LeakSanitizer and a `leak-` reproducer |
| `fuzz_canary.oom` | Allocates `ALUMBRA_FUZZ_RSS_LIMIT_MB` + 1 MiB | The memory limit and an `oom-` reproducer |
| `fuzz_canary.timeout` | Never returns for a non-empty input | `-timeout` and a `timeout-` reproducer |

`--expect <kind>` inverts the runner's verdict: a canary passes only if the run fails with exactly that
finding and saved the matching reproducer. After the real smoke run, CI reconfigures with canaries on and
runs `ctest --preset fuzz -L fuzz_canary`, so the uploaded `fuzz-reports` artefact always contains the
canaries' reproducers as evidence. Canaries are never built otherwise and are never replayed.

## Reproducing and fixing a finding

Download the `fuzz-reports` artefact, or run `ctest --preset fuzz -R '<NAME>'` locally with the same
toolchain, then replay the reproducer with the libFuzzer executable or the replay driver:

```sh
build/fuzz/test/fuzz/<NAME> <reproducer>
build/fuzz/test/fuzz/<NAME> -minimize_crash=1 -runs=10000 <reproducer>
build/debug/test/fuzz/<NAME>_replay <reproducer>
```

Fix the bug, then commit the minimised reproducer under `<target>/regressions/` with a descriptive name,
so `fuzz_replay.<NAME>` keeps it fixed on every compiler.

A finding in code that is already on `main` may be a vulnerability. Report it privately as
[SECURITY.md](../../SECURITY.md) describes instead of in a public issue or pull request, and delete its
workflow artefact once you have it: artefacts and logs of this public repository are public. The smoke
run's fixed seed and corpus normally reproduce a finding on the pull request that introduced it, so a
public reproducer usually concerns unmerged code. Longer scheduled fuzzing, corpus growth and private
crash triage are [#267](https://github.com/LuisAhumadaMartens/alumbra/issues/267).
