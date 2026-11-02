# Codec configuration and codec strings

`umbra::codec` (`umbra/codec/`, namespace `umbra::codec`) turns the codec configuration that containers and
streams carry into structured descriptions, and converts those descriptions to and from
[RFC 6381](https://www.rfc-editor.org/rfc/rfc6381) codec strings. Demuxers, `QueryDecoding` capability queries ([ADR-0029](../adr/0029-decoder-selection-and-fallback.md),
[ADR-0016](../adr/0016-penumbra-port-contract.md)), `canPlayType()` and the HLS and DASH `CODECS` attributes
all use the same parsers, as [ADR-0025](../adr/0025-demuxing-strategy.md) (D-05) requires.

The module is engine-internal. Its headers are in `umbra/codec/internal_include/umbra/codec/` and are not
installed; the installed public headers stay C++20 and Abseil-free. The structured results feed the stream types
of [media types](media-types.md) (`VideoInfo`, `AudioInfo`, `CodecId` and the `codec_string` of `StreamInfo`)
through the `ToVideoInfo`, `ToAudioInfo` and `ToCodec` helpers of each parser.

## Untrusted input

Every parser reads through the bounds-checked `ByteReader` and `BitReader` of
[byte readers](byte-readers.md), so an overrun is an explicit error and never an out-of-bounds read. Parsers
take a `std::span<const std::uint8_t>`, copy only what they keep and never read past it.

Errors follow [error conventions](errors.md) with the catalogue's canonical codes, and their messages start with
the catalogue number (`ALUMBRA-3001`, `ALUMBRA-4001`):

| Situation | Status code | Catalogue entry |
|-----------|-------------|-----------------|
| Truncated or inconsistent bytes, including every reader overrun, and values outside their legal range | `DataLoss` | `MediaCorrupt` (3001) |
| A well-formed record of a version, profile or object type that is not supported (`avcC` version 2, AAC USAC) or an unknown codec in a string | `Unimplemented` | `CodecUnsupported` (4001) |
| A malformed codec string | `InvalidArgument` | none: the string is caller input, not media |

The parsers attach no `ErrorDetail` because they do not know the track or the URL. The caller that does wraps
the status with `MakeError` and the context it knows. No new catalogue entry was needed.

Bit-level parsers (`BitstreamReader` in `reader_util.h`) latch the first failure and return zero afterwards, so
a whole syntax structure is read as plain statements and checked once. A structure whose count field exceeds a
fixed limit (for example 64 short-term reference picture sets) fails instead of looping. SPS parsing removes
emulation-prevention bytes (`00 00 03`) before reading bits.

## Records and bitstreams

| Parser | Input | Result |
|--------|-------|--------|
| `ParseAvcC`, `SerializeAvcC` | `avcC` box payload | `AvcConfig`: profile, compatibility flags, level, NAL length size, SPS, PPS and the high-profile chroma and bit-depth extension |
| `ParseH264Sps` | SPS NAL unit with its header byte | `H264Sps`: profile, level, chroma format, bit depths, coded size, cropping, visible size, SAR, VUI colour (primaries, transfer, matrix, range) and frame rate |
| `ParseHvcC`, `SerializeHvcC` | `hvcC` payload | `HevcConfig`: profile space, tier, profile, compatibility and constraint flags, level, chroma format, bit depths, NAL arrays |
| `ParseHevcVps`, `ParseHevcSps` | NAL unit with its two-byte header | profile, tier and level, chroma format, bit depths, coded size, conformance window, visible size, SAR, VUI colour and frame rate |
| `ParseVpcC`, `SerializeVpcC` | `vpcC` full box payload (version and flags first) | `Vp9Config`: profile, level, bit depth, chroma subsampling, range and colour |
| `ParseAv1C`, `SerializeAv1C` | `av1C` payload | `Av1Config` including the sequence header found in its `configOBUs` |
| `ParseAv1SequenceHeaderObu` | one OBU | `Av1SequenceHeader`: profile, level, tier, bit depth, monochrome, subsampling, colour, range, maximum frame size |
| `ParseAudioSpecificConfig` | AAC `AudioSpecificConfig` | object type, sampling frequency, channel configuration (or program config element), SBR and PS signalling |
| `ParseOpusHead`, `ParseDOps`, `SerializeDOps` | Ogg `OpusHead` (little endian) or the `dOps` payload (big endian) | `OpusConfig`: channels, pre-skip, input rate, gain and channel mapping |
| `ParseDac3`, `ParseDec3` and serializers | `dac3` and `dec3` payloads | stream, bitstream and channel information; independent and dependent substreams; the Dolby Digital Plus JOC flag |
| `ParseFlacStreamInfo`, `ParseDfLa` | `STREAMINFO` or the `dfLa` full box payload | sample rate, channels, bit depth, block and frame sizes, total samples |

Colour fields are recorded as ITU-T H.273 code points in the `ColorSpace` of `VideoInfo`, for the HDR work in
#260. A VUI or colour description that is absent leaves them *unspecified* (2), never guessed.

### Display size

The display size is the visible size (coded size minus the cropping rectangle) with its width scaled by the
sample aspect ratio, rounded to the nearest pixel. SPS cropping uses the chroma-dependent crop units of H.264
(and the field factor for interlaced content) and of HEVC. A 720x576 stream with a 16:15 SAR displays at
768x576, and a 1920x1088 coded picture with `frame_crop_bottom_offset` 4 is 1920x1080.

### Annex B and length-prefixed units

`SplitAnnexB`, `SplitLengthPrefixed`, `AnnexBToLengthPrefixed` and `LengthPrefixedToAnnexB` convert between the
two framings. A NAL length field of 1 to 4 bytes is accepted; a unit that does not fit its length field, a
length that overruns the data and a stream without a start code are errors. `AvcConfigFromAnnexB` and
`HevcConfigFromAnnexB` build `avcC` and `hvcC` configuration from the in-band parameter sets of an MPEG-TS
stream, dropping duplicates.

## Codec strings

`FormatCodecString` and `ParseCodecString` convert between `CodecDescription` and the strings below.
`ParseCodecList` splits an HLS or DASH `CODECS` attribute (with or without its quotes). `CodecIdOf` maps a
description to the `CodecId` of the media types.

| Codec | Format | Source |
|-------|--------|--------|
| H.264 | `avc1.PPCCLL` (`avc3` for in-band parameter sets): profile, constraint flags and level as two hex digits each | ISO/IEC 14496-15, RFC 6381 |
| HEVC | `hvc1` or `hev1`, then `[A\|B\|C]profile`, the compatibility flags in reversed bit order as hex, `L` or `H` and the level, and the constraint bytes as hex with trailing zero bytes omitted, for example `hvc1.1.6.L93.B0` | ISO/IEC 14496-15 Annex E |
| VP9 | `vp09.PP.LL.DD`, optionally followed by all of `CC.cp.tc.mc.FF`; every field is two decimal digits | [VP Codec ISO Media File Format Binding](https://www.webmproject.org/vp9/mp4/) |
| AV1 | `av01.P.LLT.DD`, optionally followed by all of `M.CCC.cp.tc.mc.F`; a tail equal to the defaults is omitted | [AV1 ISOBMFF binding](https://aomediacodec.github.io/av1-isobmff/#codecsparam) |
| AAC | `mp4a.40.x`, with `x` the output audio object type: 2 for LC, 5 when SBR is signalled, 29 when PS is | RFC 6381, ISO/IEC 14496-3 |
| MPEG audio | `mp4a.40.34`, `mp4a.6B` and `mp4a.69` map to MP3; any other `mp4a.OO[.x]` is parsed and keeps its object type indication | RFC 6381 |
| Opus | `opus` (`Opus` is accepted) | [Opus in ISOBMFF](https://opus-codec.org/docs/opus_in_isobmff.html) |
| AC-3, E-AC-3 | `ac-3`, `ec-3` | ETSI TS 102 366 |
| FLAC | `fLaC` (`flac` is accepted) | FLAC in ISOBMFF |
| Dolby Vision | `dvh1.PP.LL`, `dvhe`, `dav1`, `dvav`, `dva1` with a two-digit profile and level | Dolby Vision profiles and levels |

Parsing is case-sensitive for the sample entry. The hex digits of `avc1` and `hvc1` are accepted in either case;
the generated `avc1` digits are lowercase and the `hvc1` digits uppercase. VP9 and AV1 colour fields are
generated with two digits and accepted with two or three, so a reserved code point above 99 still round-trips.
The optional groups of VP9 and AV1 are all-or-none, as their bindings require.

Strings generated from the sample media of the repository's tests match the `CODECS` values that ffmpeg's DASH
muxer writes for the same files (`avc1.640028`, `mp4a.40.2`, `ac-3`, `ec-3`, `opus`, `av01.0.01M.08`), and the
HEVC, VP9 and AV1 strings reproduce the examples of their bindings.

## Tests

`umbra_codec_unittests` (`test/umbra/codec/`) has one suite per record type. The record fixtures are the boxes
that ffmpeg, x264, x265, libvpx and libaom wrote for short generated clips (hex in `fixtures.h`); SPS, VPS and
sequence headers with optional syntax elements (VUI, scaling lists, reference picture sets, operating points,
program config elements) are built bit by bit in the tests. Every record is also checked for serialise and parse
round trips and for an error on every truncation.

`codec_config_fuzzer` ([fuzz targets](../../test/fuzz/README.md)) takes a selector byte and exercises every
parser above plus Annex B conversion, emulation-prevention removal and codec string parsing. A successful parse
must produce a geometry that is non-empty and inside the coded picture, serialise, and parse back to the same
value, and a codec string that parses must format and parse back to the same description. Any other outcome
must be an error of the three codes above.
