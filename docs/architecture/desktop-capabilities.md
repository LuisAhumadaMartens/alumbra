# Desktop capability query

`penumbra::desktop::DesktopCapabilityProvider` implements `CapabilityProvider::QueryDecoding` for the desktop port
([ADR-0016](../adr/0016-penumbra-port-contract.md), [port contract](port-contract.md)). The engine uses the answer for
`canPlayType()`, source selection and ABR filtering ([ADR-0033](../adr/0033-abr-algorithm-and-bandwidth-estimation.md)).
Desktop M1 decodes in software ([ADR-0029](../adr/0029-decoder-selection-and-fallback.md)), so every answer describes
FFmpeg's native decoders and dav1d.

## Answer

| Field | Desktop value |
|-------|---------------|
| `supported` | The codec is in the decoder table, its profile and bit depth are accepted, no limit disables it, and the decoder is compiled in. |
| `smooth` | Video: the luma sample rate (width x height x frame rate) is within the codec's budget. Audio: always true once supported. |
| `power_efficient` | Always false. Software decoding is never power efficient. |
| `preferred_output_mode` | `kTexture` |
| `accepts_fragments` | False. The engine demuxes; the desktop port takes samples. |

Unknown support is `supported = false`, never an error. A probe failure is returned as a non-OK `Status` and is not
cached. A malformed request (negative bitrate, size or frame rate) is `kInvalidArgument`.

## Inputs

- **Codec.** `StreamInfo::codec` and the RFC 6381 `codec_string` are both accepted. When both are present they must
  agree. A string alone is enough. With neither, the answer is unsupported.
- **Container.** The MIME type, when present, must name a container the desktop demuxers read (MP4, WebM, Matroska,
  MPEG-TS, MP3, FLAC, Ogg, AAC). An empty MIME type skips the check.
- **Profile and bit depth.** Parsed from the codec string and checked per codec: H.264 profiles 66 to 244, HEVC
  profiles 1 to 4, VP9 profiles 0 to 3 with a matching depth, AV1 profiles 0 to 2 at 8, 10 or 12 bits.
- **HDR, key system, encryption.** Any of these makes the answer unsupported until HDR (#260, M11) and key systems
  (#235) land.
- **Limits.** `DecodeLimits::disabled_decoders` (`decode.disabled_decoders`) lists decoders by `h264`, `hevc`, `vp9`,
  `av1`, `aac`, `opus`, `ac3`, `eac3`, `flac` or `mp3`. `DecodeLimits::max_video_height` (`decode.video.max_height`)
  rejects taller video, with 0 meaning unlimited.

The codec-string parser in `codec_string.cc` is minimal and internal. It is replaced by the shared parser when #81
lands.

## Smoothness

Each video decoder has a budget in luma samples per second, so a higher frame rate shrinks the largest smooth size:

| Codec | Budget | Largest smooth sizes |
|-------|--------|----------------------|
| H.264 | 4K at 60 fps | 4K60, 1440p120, 1080p240 |
| HEVC, VP9 | 4K at 30 fps | 4K30, 1080p120 |
| AV1 (dav1d) | 1080p at 60 fps | 1080p60, 1440p30 |

Unknown geometry is not smooth. An unknown frame rate is treated as 60 fps. The budgets are conservative defaults for a
mainstream multi-core desktop and are not yet calibrated against measurements. Display refresh rate does not enter
the answer yet (see below).

## Probing and caching

`DecoderProbe` reports whether a decoder is compiled in. `SoftwareDecoderProbe` asks FFmpeg
(`avcodec_find_decoder_by_name`) and dav1d (`dav1d_version`). Tests inject a fake probe, so they depend neither on the
FFmpeg component list nor on the machine's GPU. One test opens every table decoder for real.

Answers are cached per request, keyed on every field that can change the answer. The cache is bounded (512 entries,
cleared when full) and thread safe. Limits are fixed at construction, so nothing invalidates it.
`capability_provider_benchmark` measures cached and uncached queries; a cached query takes under a microsecond.

## Not yet covered

- Display capabilities from [ADR-0030](../adr/0030-desktop-graphics-audio-and-ui-renderer.md): per-display refresh rate
  and VRR content-multiple matching need SDL display information, after #68.
- Opening a decoder per profile and bit depth. The probe checks only that the decoder exists.
- Hardware decoding, after M1.
- Windows ([ADR-0031](../adr/0031-windows-support-scope-and-toolchain.md)) has no capability code.
- `CanChangeType` always returns false, so the engine recreates the player on a codec change.
- The component list of #85 may remove decoders from FFmpeg; the probe then reports them unsupported.
