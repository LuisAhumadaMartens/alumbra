# Media types

The shared vocabulary of the port contract ([ADR-0016](../adr/0016-penumbra-port-contract.md)): time, rationals,
encoded samples, stream descriptions and decoded-output descriptors. Demuxers in Umbra produce these values and
ports consume them, so they live in the **Penumbra interface** (`penumbra/interface/include/penumbra/interface/`,
namespace `penumbra`), which may not include Umbra headers ([layering](layering.md)).

## Placement and dependencies

- **Header-only.** `penumbra::interface` stays an `INTERFACE` library. The types are small value types and inline
  functions, so no compiled library, export change or link dependency is needed.
- **Standard C++20 only.** The headers include only the C++ standard library. Embedders and ports compile them at
  exactly C++20 ([ADR-0013](../adr/0013-cpp-language-standard.md)), and the installed-consumer test compiles each
  one alone in that mode. Abseil is deliberately not used in these public headers: embedders would otherwise need
  the same Abseil release, and Abseil's ABI changes with every LTS. Tests use Abseil freely.
- **Abseil integration without Abseil headers.** Rationals, times, timestamps and encoded samples provide
  `ToString()` and an `AbslStringify` hook that only calls `sink.Append`; stream types, codec IDs and encryption
  checks also provide stringify hooks. `absl::StrCat`, `absl::StrFormat("%v")` and Abseil logging print these
  values, but the headers do not include Abseil.
- **Errors are values.** Operations that can fail return `std::optional` (rescaling, slicing, rational creation) or
  a small check enum (`EncryptionCheck`). [ADR-0018](../adr/0018-error-model-and-recovery.md) settles D-09:
  internally the engine uses `absl::Status` / `absl::StatusOr<T>` with structured details and a public error
  catalogue. These C++20-only interface helpers retain their lightweight results; engine callers translate
  failures into that model. The catalogue and detail helpers are implemented by #49, not by these media types.

## Headers

The [C++ API reference](https://luisahumadamartens.github.io/alumbra/api/cpp/) documents every type and function
below, with compiled examples.

| Header | Types |
|--------|-------|
| `rational.h` | `Rational`, `Rounding`, `Rescale(int64_t, Rational, Rational, Rounding)` |
| `media_time.h` | `TimeDelta`, `MediaTime`, `Timestamp`, conversions between them |
| `shared_bytes.h` | `SharedBytes` |
| `encryption_info.h` | `EncryptionScheme`, `SubsampleEntry`, `EncryptionPattern`, `EncryptionInfo`, `EncryptionCheck`, `ValidateEncryption` |
| `stream_info.h` | `StreamType`, `CodecId`, `SampleFormat`, `ColorSpace`, `VideoInfo`, `AudioInfo`, `TextInfo`, `StreamInfo` |
| `encoded_sample.h` | `EncodedSample` |
| `decoded_frame.h` | `PixelFormat`, `VideoPlane`, `DecodedVideoFrame`, `DecodedAudioBuffer` |

## Rationals and rescaling

- `Rational` holds an `int64_t` numerator and denominator, always normalised: the denominator is positive, the
  fraction is reduced, and zero is `0/1`. `Rational::Create(n, d)` returns `std::nullopt` for a zero denominator or
  `INT64_MIN` in either term. `Rational::Of(n, d)` is `consteval`, so an invalid constant fails to compile.
- `Rescale(value, from, to, rounding)` returns `value × from ÷ to` rounded as requested, or `std::nullopt` when a
  time base is not positive, either term of the reduced scale `from / to` exceeds `INT64_MAX`, or the result
  does not fit in `int64_t`. The scale-size restriction can reject a conversion even when its rounded result
  would fit: rescaling `1` from `1/INT64_MAX` to `INT64_MAX/1` with `kDown` returns `std::nullopt`, not `0`.
  It multiplies into 128 bits and divides back
  with portable 64-bit arithmetic, so there is no intermediate overflow and no undefined behaviour anywhere in the
  `int64_t` range, and it is `constexpr`.
- Rounding is always explicit: `kTowardZero`, `kAwayFromZero`, `kDown` (towards −∞), `kUp` (towards +∞) and
  `kNearest` (ties away from zero). Every mode is monotonic, and the result is within one unit of the exact value
  (half a unit for `kNearest`).

## Time

| Type | Meaning | Representation |
|------|---------|----------------|
| `TimeDelta` | A signed duration | `int64_t` microseconds |
| `MediaTime` | A position on the media timeline | `int64_t` microseconds |
| `Timestamp` | An exact container value, for example `3003` in `1/90000` | `int64_t value` plus `Rational time_base` |

- **Microseconds** are the engine's working unit because the port APIs speak them: Starboard, WebCodecs,
  MediaCodec (`presentationTimeUs`) and FFmpeg's `AV_TIME_BASE`. No floating-point seconds are used internally.
- **Exactness** stays in `Timestamp`. Samples carry their container timestamps unchanged, so remuxing (for
  example MPEG-TS to fMP4 for MSE) and gapless trimming never accumulate microsecond rounding. Conversions to
  `MediaTime` and `TimeDelta` (`ToMediaTime`, `ToTimeDelta`), back (`ToTimestamp`) and between time bases
  (`Rescale(Timestamp, …)`) take an explicit rounding mode and return `std::nullopt` on overflow.
- **Sentinels.** `TimeDelta` and `MediaTime` reserve `INT64_MIN` for *unknown*, `INT64_MIN + 1` for −∞ and
  `INT64_MAX` for +∞; finite values lie strictly between. Arithmetic saturates to ±∞ instead of overflowing,
  unknown propagates, and ∞ − ∞ is unknown. The default ordering sorts unknown < −∞ < finite < +∞.
  `Timestamp::kUnknown` (`INT64_MIN`) marks a missing container value, which converts to the unknown sentinel; a
  rescale whose result would collide with it reports overflow.
- `MediaTime − MediaTime` is a `TimeDelta`, and `MediaTime ± TimeDelta` is a `MediaTime`. Adding two media times
  does not compile.

## Samples and shared data

- `SharedBytes` is an immutable, reference-counted byte range. `Adopt` takes ownership of a vector without copying,
  `Wrap` adopts memory that another owner keeps alive (for example a network buffer or a platform object), `Copy`
  copies, and `Slice` returns a sub-range that shares the same storage or `std::nullopt` when out of range. Copies
  of a `SharedBytes` share the bytes; the reference count is atomic, so ranges can move between sequences
  ([ADR-0015](../adr/0015-threading-and-task-runners.md)). The bytes themselves are never written after creation.
  Moving a `SharedBytes` leaves the source empty (`data()` is null and `size()` is 0), so a moved-from value is
  never a null pointer with a nonzero size.
- `EncodedSample` carries one access unit: `track_id`, `data`, `pts`, `dts` and `duration` as `Timestamp`s in the
  stream's time base (unknown by default), key-frame and discontinuity flags, an optional new `StreamInfo` on a
  configuration change, `trim_start`/`trim_end` for encoder priming and padding (unknown means none),
  `hdr_dynamic_metadata` bytes and optional `EncryptionInfo`. A demuxer slices samples out of the downloaded
  segment, and copying a sample copies references, so the bytes reach the port without being copied.
- `EncryptionInfo` holds the scheme (`cenc` or `cbcs`), the 16-byte key ID, the IV, the subsample map (clear and
  encrypted byte counts) and an optional pattern. `ValidateEncryption` checks that the IV is 8 or 16 bytes for
  `cenc` and 16 bytes for `cbcs`, that a pattern appears only with `cbcs` and fits its 4-bit fields, and that the
  subsample map, when present, sums exactly to the sample size. An empty map means the whole sample is
  encrypted. Where the CDM attaches is D-27's decision.

## Streams

`StreamInfo` describes a track: codec (`CodecId` plus the RFC 6381 string, for example `avc1.64001f`), codec
configuration bytes (for example `avcC`), time base, BCP 47 language, and a `std::variant` of `VideoInfo`,
`AudioInfo` or `TextInfo`, from which `type()` is derived.

- `VideoInfo`: coded and display size, sample aspect ratio (default `1/1`), frame rate (`0/1` when unknown) and a
  `ColorSpace` placeholder with ITU-T H.273 code points (2, unspecified, by default) and the range flag. HDR static
  metadata will extend it.
- `AudioInfo`: sample rate, channel count, a channel mask using WAVE/FFmpeg channel bits (0 when unspecified) and
  the sample format.
- `TextInfo` is a placeholder until D-15 defines the cue model.

## Decoded output

`DecodedVideoFrame` and `DecodedAudioBuffer` describe memory the **port** owns: timing, geometry or audio format,
plane pointers (or none, for `PixelFormat::kOpaque` hardware frames) and a `std::shared_ptr<const void> storage`
that keeps the port's buffer, surface or platform object alive for as long as any copy of the descriptor exists.
The reference player's frame queue (ADR-0016) moves these descriptors, never pixels. Moving a descriptor transfers
`storage` and resets the source to the default, empty descriptor, so a moved-from frame never keeps plane pointers
into memory it no longer owns.

## Tests

`penumbra_interface_test` (GoogleTest, `test/penumbra_interface/`) covers:

- rescaling between common time bases (90 kHz, 48 kHz, 44.1 kHz, µs, ms, 1001/30000, 1001/24000, 1001/60000,
  1/25, 1/1) for random values in every rounding mode, checked against exact 128-bit arithmetic, plus
  monotonicity and a round trip within one unit;
- the MPEG-TS 33-bit wrap range and both ends of `int64_t`, which run under UBSan in the `asan` preset;
- sentinel propagation and saturation, subsample validation, slice sharing and owner lifetime;
- a fake demuxer and fake port proving that sample bytes arrive at the port at the same address they had in the
  downloaded segment.
