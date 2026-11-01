# Byte and bit readers

Every parser of untrusted bytes in Umbra reads through `umbra::base::ByteReader` and
`umbra::base::BitReader`, so bounds checks live in one fuzzed place
([`byte_reader_fuzzer`](../../test/fuzz/README.md)). Both are engine-internal: their headers are in
`umbra/base/internal_include/umbra/base/` and are not installed.

A reader is a view of a `std::span<const std::uint8_t>` with a cursor. It does not own or copy the bytes,
so the buffer must outlive it, and it is not thread-safe. Every read either succeeds and advances the
cursor or fails and leaves the cursor exactly where it was, so a parser can report an error or try another
interpretation without resynchronising.

## `ByteReader`

| Method | Reads |
|--------|-------|
| `ReadU8`, `ReadU16`, `ReadU24`, `ReadU32`, `ReadU64` | Unsigned big-endian integers of 1, 2, 3, 4 and 8 bytes |
| `ReadUnsigned(n)` | An unsigned big-endian integer of `n` bytes, 1 to 8 (for example a length field whose width the stream declares) |
| `ReadBytes(n)` | A span of the next `n` bytes, pointing into the original buffer |
| `Skip(n)` | Nothing; advances `n` bytes |

`size()`, `position()` and `remaining()` are in bytes. Construct a nested reader for a bounded structure,
such as an MP4 box, from the span that `ReadBytes` returns.

## `BitReader`

Bits are read most significant bit first, as in H.264, HEVC, AAC and MPEG-TS headers.

| Method | Reads |
|--------|-------|
| `ReadBits(n)` | `n` bits, 0 to 64, as an unsigned value |
| `ReadFlag()` | One bit as a `bool` |
| `SkipBits(n)` | Nothing; advances `n` bits |
| `SkipToByteBoundary()` | Nothing; advances to the next byte boundary, if not already on one. It cannot fail. |
| `ReadExpGolomb()` | An unsigned Exp-Golomb code, `ue(v)` in [ITU-T H.264](https://www.itu.int/rec/T-REC-H.264) §9.1 |
| `ReadSignedExpGolomb()` | A signed Exp-Golomb code, `se(v)` in H.264 §9.1.1 |

`position()` and `remaining()` are in bits; `is_byte_aligned()` reports whether the cursor is on a byte
boundary. Exp-Golomb codes are limited to 31 leading zero bits, as H.264 and HEVC require of `ue(v)`, so
`ReadExpGolomb` returns 0 to 2³² − 2 and `ReadSignedExpGolomb` returns −(2³¹ − 1) to 2³¹ − 1.

## Errors

Fallible methods return `[[nodiscard]] absl::Status` or `absl::StatusOr<T>` (see
[error conventions](errors.md)):

| Status code | When |
|-------------|------|
| `OutOfRange` | The read needs more bytes or bits than remain, including an Exp-Golomb code cut off by the end of the buffer. More data could make the same read succeed. |
| `InvalidArgument` | The request or the data can never be valid: `ReadUnsigned` outside 1 to 8 bytes, `ReadBits` outside 0 to 64 bits, or an Exp-Golomb code with 32 or more leading zero bits. |

Messages state the requested and remaining sizes and the offset, never the bytes themselves. The readers
attach no `ErrorDetail`: they do not know what they are parsing. A parser maps their failures to its own
catalogue code, for example `MediaCorrupt` (`DataLoss`) for a truncated MP4 box or `ManifestInvalid`
(`InvalidArgument`) for a malformed playlist, as [error conventions](errors.md#canonical-conventions)
describe.
