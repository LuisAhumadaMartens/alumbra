# Conformance assets

Conformance tests read their media from [`manifest.yaml`](manifest.yaml), a
versioned list of test streams with provenance, licence evidence and integrity
data, as [ADR-0021](../../docs/adr/0021-testing-strategy.md) requires. Large media
is referenced, never committed ([ADR-0012](../../docs/adr/0012-repository-topology-and-packaging.md)):
[`tools/conformance/assets.py`](../../tools/conformance/assets.py) downloads or
generates each asset into a local, content-addressed cache and verifies it.
Third-party works are credited on the generated
[attribution page](ATTRIBUTION.md).

## Entry kinds

| Kind | Source | Integrity | Where it runs |
|------|--------|-----------|---------------|
| `download` | A file fetched from one of its `urls`, tried in order. | `sha256` and `size` are required and checked while downloading and on every cache hit. | Blocking tests, once fetched. |
| `generated` | A committed FFmpeg `lavfi` script in [`generators/`](generators/). | The output is probed with `ffprobe` against `expected` (codecs, profile and level, dimensions, frame rate, frame count, sample rate, channels, duration). Encoder versions change the bytes, so no digest is pinned. | Blocking tests, offline. |
| `remote` | A public HLS or DASH presentation streamed from its publisher. | The `urls` must answer with an HLS playlist or a DASH MPD. Live and moving content has no fixed digest or duration. | The non-blocking **Probe remote conformance vectors** job only. |

## Manifest format

The manifest is a strict YAML subset, so the fetcher and CMake need only the
Python standard library: block mappings and sequences, one-line flow sequences
(`[a, b]`), double-quoted strings (escapes `\"` and `\\` only), decimal
integers, `true` and `false`. Comments, anchors, aliases, tags, block scalars,
flow mappings, single quotes, tabs, trailing spaces and plain scalars that YAML
would read as another type (`yes`, `null`, `1.5`, `2024-01-01`, `0x1f`, …) are
rejected with a line number; quote such strings. CI checks that PyYAML parses the
manifest to the same data.

The top level has three keys:

- `schema`: the format version, currently `1`. It changes only when the field
  layout changes; it is not a result identity. Results identify the manifest by
  the source revision ([ADR-0021](../../docs/adr/0021-testing-strategy.md)).
- `tags`: the tag vocabulary, mapping each tag to its meaning. Every declared
  tag must be used, and every used tag declared.
- `entries`: the cases.

Each entry has these fields:

| Field | Required | Meaning |
|-------|----------|---------|
| `id` | always | Stable lowercase identifier joined by hyphens, such as `tears-of-steel-stereo-opus`. Never reuse or rename an id. |
| `title` | always | Human-readable name. |
| `kind` | always | `download`, `generated` or `remote`. |
| `urls` | `download`, `remote` | Distinct `https` URLs. Downloads try them in order; every one must serve the same bytes. |
| `sha256`, `size` | `download` | Lowercase hex SHA-256 (quoted) and size in bytes, computed from a real download. |
| `file` | `download`, `generated` | File name inside the cache; the extension matters to some tools. |
| `generator` | `generated` | Script under `test/conformance/generators/` that writes its single argument. |
| `provenance` | always | Who made it, where it came from and its canonical location. |
| `licence` | always | See below. |
| `redistributable` | always | `true` only when the licence evidence grants asset-specific permission to redistribute these bytes. |
| `protocol` | always | `progressive`, `hls` or `dash`. |
| `container` | always | Container of the file or of the segments: `mp4`, `mov`, `ogg`, `flac`, `srt`, … |
| `codecs` | always | RFC 6381 `codecs` values (`avc1.4d401f`, `mp4a.40.2`); where none exists, the WebCodecs registry name (`mp3`, `opus`, `flac`) or the text format (`srt`, `wvtt`). Download and generated entries may only use codecs the media check maps to an FFmpeg decoder (`manifest.CODEC_PREFIXES` and `CODEC_NAMES`). |
| `tags` | always | Feature tags from the vocabulary. They must include the `protocol`; `generated` and `remote` are required exactly for those kinds, and `live` only appears on remote entries. |
| `platforms` | always | Platforms the expectation covers, from the [settings platform list](../../settings/meta-schema.toml). |
| `expected` | always | See below. |

`licence` has these fields:

- `url` (required): the licence text or the publisher's terms of use.
- `evidence` (required): the page that states those terms for this asset. A
  licence of the hosting site, tool or repository alone is not evidence.
- `spdx`: the SPDX identifier, or `LicenseRef-…`. Omit it when no licence is granted.
- `attribution`: the credit line the licence asks for. Required for every
  third-party entry.
- `notes`: exclusions and restrictions, such as excluded logos or "stream only".

`expected` describes the behaviour of a conforming player:

- `outcome` (required): `plays`, or `error` with `error` naming an
  [error catalogue](../../errors/catalogue.toml) entry such as `CodecUnsupported`.
- `duration`: `value` ticks of a `timescale` per second, so `17620` at `24` is
  17620 frames of 24 fps. Live entries have none.
- `tracks`: in stream order, each with `type` (`video`, `audio` or `text`), a
  `codec` from `codecs` and optional `width`, `height`, `frame_rate`
  (integer frames per second), `frames`, `sample_rate`, `channels` or `cues`.
  When `tracks` is present, every listed codec needs a track.

## Rules for adding an entry

1. Prefer a generated clip. Use third-party media only for behaviour a
   generator cannot produce.
2. Verify the licence on the publisher's own page and record that page as
   `licence.evidence`. Being reachable at a public URL, or being shipped with a
   tool, does not grant rights. If the terms are unclear, do not add a
   download; at most add a `remote` entry with `redistributable: false`, and
   ask in the pull request.
3. Downloads need verified redistribution permission (`redistributable: true`
   with a permissive or Creative Commons Attribution SPDX licence), because the
   cache, CI caches and mirrors copy the bytes.
4. Compute `sha256` and `size` from an actual download, and fill `expected`
   from the file, for example with
   `ffprobe -count_packets -show_streams -show_format`, then run
   `python3 tools/conformance/assets.py fetch --ids <id> --probe`.
5. Regenerate the attribution page with
   `python3 tools/conformance/assets.py attribution` and commit it.

## Fetching and verifying

```sh
python3 tools/conformance/assets.py validate
python3 tools/conformance/assets.py list --tags progressive,h264
python3 tools/conformance/assets.py fetch --tags progressive,h264
python3 tools/conformance/assets.py --offline fetch --tags generated
python3 tools/conformance/assets.py path --ids frame-numbered-h264-mp4
python3 tools/conformance/assets.py probe --tags remote
```

- **Selection.** `--tags a,b` selects the entries that have every listed tag,
  `--ids` names entries, and `--kind` filters by kind. Unknown tags, kinds or ids
  and empty selections are errors.
- **Cache.** `--cache`, else `ALUMBRA_ASSET_CACHE`, else
  `$XDG_CACHE_HOME/alumbra/assets` (default `~/.cache/alumbra/assets`).
  Downloads live at `sha256/<digest>/<file>`; generated clips at
  `generated/<id>/<recipe>/<file>`, where the recipe hashes the id and the
  generator script, so editing a script regenerates its clips. Files are written
  to `tmp/` and moved into place only after verification.
- **Integrity.** A digest or size mismatch fails with the entry id, the URL and
  both digests, and never leaves a file in the cache. A cached file is
  re-hashed on every use; a corrupt one is removed and fetched again.
- **Offline.** `--offline` or `ALUMBRA_ASSET_OFFLINE=1` forbids network access.
  Cached downloads and generated clips still work; an uncached download fails
  visibly. A second run is served from the cache without network access.
- **Retries.** Each URL gets `--attempts` tries (default 3) with exponential
  backoff of 1 s, 2 s, 4 s, … capped at 30 s, for connection errors and HTTP
  408, 425, 429 and 5xx. Other HTTP errors, integrity failures and local write
  errors move straight to the next URL. Redirects from `https` to another
  scheme are refused. `probe` checks download URLs with `HEAD`, falling back to
  a one-byte ranged `GET` when a server refuses `HEAD` or omits the length.
- **Exit status.** `0` success; `1` failure, including integrity, local errors
  and an uncached download in offline mode; `2` invalid manifest or selection;
  `77` when every source was only unreachable (a network outage after all
  retries, or `probe` in offline mode).
- **Generated clips.** A cached clip is re-probed on every use and regenerated
  when it no longer matches `expected`. A missing or failing `ffprobe` is
  reported as a tool error and leaves the cache alone.

Generating clips needs `bash` and an FFmpeg build with `libx264` and the
`lavfi` sources on `PATH`, or set `FFMPEG` and `FFPROBE`. FFmpeg runs as a host
tool: it is not linked into or shipped with Alumbra, and its output is not
committed. CI installs the Ubuntu `ffmpeg` package for this.

## Synthetic clips

| Script | Entry | Content |
|--------|-------|---------|
| [`frame_numbered.sh`](generators/frame_numbered.sh) | `frame-numbered-h264-mp4` | 300 frames of 640x360 at 30 fps; FFmpeg's `testsrc` counter shows each frame's index. H.264 High 3.0 with a key frame every second and no B-frames. |
| [`tone.sh`](generators/tone.sh) | `tone-1khz-2khz-aac-m4a` | 10 s of 48 kHz stereo at -20 dBFS, 1 kHz on the left and 2 kHz on the right, in AAC-LC. |
| [`av_sync.sh`](generators/av_sync.sh) | `av-sync-flash-beep-h264-aac-mp4` | The frame counter with a full white frame on every 30th frame, and a one-frame (1600-sample) 1 kHz beep that starts at the same instant, for 10 s. |

`testsrc` draws its counter with built-in segments, so no font is involved.

## CTest labels

Every entry is registered as the CTest test `conformance.asset.<id>`, labelled
`conformance` plus the entry's tags. Downloads and generated entries run
`assets.py fetch --ids <id>`; remote entries run `assets.py probe --ids <id>`
and report an outage as skipped (exit 77), never as a pass. Downloads are
required inputs, so any exit other than 0, including 77, fails their test, as
ADR-0021 requires for unavailable offline inputs. Each test is also
a fixture (`FIXTURES_SETUP conformance.asset.<id>`) that later conformance
tests can require.

The tests are registered but disabled unless the build is configured with
`-DALUMBRA_CONFORMANCE_ASSETS=ON`, because they fetch media. Tag selection maps
directly to labels; anchor the expressions, since `ctest -L` matches regular
expressions and repeated `-L` options must all match:

```sh
cmake --preset debug -DALUMBRA_CONFORMANCE_ASSETS=ON
python3 tools/conformance/assets.py fetch --tags progressive,h264
ctest --preset debug -L '^progressive$' -L '^h264$'
ctest --preset debug -L '^generated$'
```

The always-enabled `conformance_tools` label covers the manifest validation,
the attribution-page check, the tool unit tests and `conformance_tag_labels`,
which runs `ctest --show-only` for every tag and for `progressive,h264` and
fails unless the CTest label selection equals `assets.py list --tags`.

## Continuous integration

- **Validate conformance manifest** (blocking, part of `ci-ok`): validates the
  manifest, rejects a stale attribution page, runs the tool unit tests (a local
  `http.server` fixture, no real network) with PyYAML parity required, and
  generates and verifies every synthetic clip with `ALUMBRA_ASSET_OFFLINE=1`,
  then fails unless a second run serves every clip from the cache.
- **Probe remote conformance vectors** (non-blocking, `continue-on-error`, not
  required by `ci-ok`): probes the remote streams and every download URL, then
  downloads and probes the downloads of up to 16 MiB. A failure there reports
  an outage or link rot; it is not merge evidence.
