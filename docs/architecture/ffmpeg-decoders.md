# FFmpeg decoders

The desktop port decodes H.264, HEVC, VP8, VP9, Theora, AAC, Vorbis, Opus, MP3, AC-3, E-AC-3 and FLAC with FFmpeg's native libavcodec
decoders (#85). The code is in `penumbra/desktop/` (target `penumbra::desktop_decode`, not installed). It follows the
decoder chain contract of [decoder chain](decoder-chain.md), the software-only rule of
[ADR-0029](../adr/0029-decoder-selection-and-fallback.md) and the licensing rules of
[ADR-0014](../adr/0014-dependency-and-licensing-policy.md). Hardware decoding is #341, and AV1 is the dav1d decoder of
#86 ([dav1d decoder](dav1d-decoder.md)).

## The narrowed FFmpeg build

FFmpeg is built by the overlay port `cmake/ports/ffmpeg`, which `vcpkg.json` lists under `overlay-ports`. It copies the
pinned vcpkg port (FFmpeg 9.0.2 with its patches) at `port-version` 5 and changes three things:

- The manifest offers only the `avcodec`, `avformat` and `swresample` features, so `gpl`, `version3`, `nonfree` and every
  external library are unreachable.
- The portfile adds `--disable-gpl --disable-version3 --disable-nonfree --disable-programs --disable-network
  --disable-protocols --disable-everything` to the port's own `--disable-autodetect --enable-shared --disable-static`
  and then enables the components of `cmake/ports/ffmpeg/components.txt`. On macOS it no longer enables the AppKit,
  AVFoundation, Core Image, AudioToolbox and VideoToolbox frameworks, so there are no hwaccels or platform decoders.
- The portfile stops with an error on Windows, on a static build, and when a gpl, version3, nonfree, protocol,
  encoder, muxer, hwaccel or external library option reaches the configure command.

`components.txt` is the single list of what the build contains:

| Kind | Components |
|------|------------|
| Decoders | `h264`, `hevc`, `vp8`, `vp9`, `theora`, `vp3` (Theora dependency), `aac`, `vorbis`, `opus`, `mp3float`, `ac3`, `eac3`, `flac` |
| Parsers | `h264`, `hevc`, `vp8`, `vp9`, `vp3`, `av1`, `aac`, `opus`, `mpegaudio`, `ac3`, `flac` |
| Demuxers | `mov` (MP4 and M4A), `matroska` (Matroska and WebM), `ogg`, `avi`, `flv`, `mpegts`, `flac`, `mp3`, `ac3`, `eac3`, `aac`, `ivf`, `h264`, `hevc` |
| Bitstream filters | `vp9_superframe_split`, which configure selects for the VP9 decoder |
| Encoders, muxers, protocols, filters, hwaccels, devices | none |

There is no AV1 decoder in this build: libavcodec's native AV1 decoder needs a hardware accelerator, and the libdav1d
wrapper stays disabled because dav1d is used directly. The AV1 parser is there for the demuxers.

The demuxer list is the reviewed enable-list of the libavformat [fallback demuxer](fallback-demuxer.md) (#322). It covers
the long tail (`avi`, `flv`, `ogg`) and the demuxers for the own-parser containers (`mov`, `matroska`, `mpegts`, `mp3`,
`aac`), which the automatic retry needs. #85 also uses them to read the test clips through a custom `AVIOContext`.
Parsers are limited to the decoders' codecs.

`ffmpeg_components_test` reads `components.txt` and asserts against the built libraries that the decoders, parsers,
demuxers and bitstream filters are exactly those listed, that no encoder, muxer, protocol or native AV1 decoder
exists, and that `avcodec_configuration()` carries the baseline flags and none of the forbidden ones.
`ffmpeg_license_test` keeps asserting the LGPL licence strings, the absence of `--enable-gpl` and `--enable-nonfree`,
and shared linkage.

### Patent-encumbered decoders

ADR-0014 asks #85 to define a build option that removes the decoders covered by patent pools. Lines of
`components.txt` marked `patent` (the decoders `h264`, `hevc`, `aac`, `ac3` and `eac3`, and their parsers) are skipped
when a triplet sets `ALUMBRA_FFMPEG_PATENT_DECODERS` to `OFF`. They are then missing from libavcodec, not merely
unselected at runtime. The result is a desktop port that decodes VP8, VP9, Theora, Vorbis, AV1, Opus, MP3 and FLAC only. The capability
provider reports the missing decoders as unsupported through its probe, so `canPlayType()` stays truthful. CI does not
build this variant, and the option is not a patent-clearance guarantee.

### Licence inventory

The reviewed exception for `ffmpeg` in `tools/ci/licence_policy.json` is keyed on `9.0.2#5` and records the overlay
evidence. `THIRD_PARTY.md` shows the new version. The corresponding source, patches, configure command and build scripts
that accompany a distributed binary are packaged by #273; until then no FFmpeg binary is distributed.

## Factory and candidates

`DesktopComponentFactory` implements `ComponentFactory`. `GetDecoderCandidates` returns one candidate per decoder that
the build contains (asked of FFmpeg through `DecoderProbe`), named `ffmpeg.<decoder>`: `ffmpeg.h264`, `ffmpeg.hevc`,
`ffmpeg.vp8`, `ffmpeg.vp9`, `ffmpeg.theora`, `ffmpeg.aac`, `ffmpeg.vorbis`, `ffmpeg.opus`, `ffmpeg.mp3float`,
`ffmpeg.ac3`, `ffmpeg.eac3` and `ffmpeg.flac`. Each has
`kind = kBundledSoftware`, `output = kCpuBuffer`, `secure = false`, its one codec and no size limit. Text streams have
no candidates. `CreateDecoder` fails with `kNotFound` for an unknown candidate, `kInvalidArgument` for a codec the
candidate does not decode, a missing callback or a thread count outside 0 to 64, and `kFailedPrecondition` for an
encrypted stream. `CreateVideoSink` and `CreateAudioSink` return `kUnimplemented` until the SDL renderer and audio
output exist.

`penumbra::reference_player` stays uninstalled. `penumbra_desktop`, the installed target, does not link the decoders or
the reference player. The desktop player factory that does will have to install the reference player in the same change.

## Decoder behaviour

`FfmpegDecoder` runs on a worker thread of its own (`WorkerDecoder`), so the control sequence never waits for a decode.
Decoded events may therefore arrive on any thread, as the contract allows.

- **Grants.** The decoder emits `NeedsData` once after creation, again each time it has decoded everything it was
  given, and after each `Flush`. A grant is 8 samples for video and 16 for audio. A write must carry the current ticket
  and at most the granted number of samples; anything else is `kInvalidArgument`. `EndOfStream` needs a grant, drains
  the decoder and then reports `StateChanged` with `kEndOfStream`.
- **Flush.** `Flush(ticket)` waits for the sample in flight, discards the queue, calls `avcodec_flush_buffers` and emits
  a grant under the new ticket. Events of the old ticket can only precede its return.
- **Send and receive.** Each sample goes to `avcodec_send_packet`, and every available frame comes back through
  `avcodec_receive_frame`. `EAGAIN` and `EOF` end a receive loop, and a send that answers `EAGAIN` is retried after the
  frames are taken.
- **Threads.** Video uses frame and slice threading with `decode.video.threads` threads, where 0 lets FFmpeg choose
  (`DesktopDecoderOptions::video_threads`; the settings key arrives with #327). Audio decoders run single threaded.
- **Time.** Packet timestamps are in the stream's time base, so B-frame reordering produces frames with the
  presentation times of the samples that carried them. Frame times convert to microseconds with rounding to nearest. A
  frame without a duration takes the reciprocal of the stream's frame rate, and an audio buffer without a time follows
  the previous one.
- **Priming and padding.** `EncodedSample::trim_start` and `trim_end` become FFmpeg skip-samples side data, converted
  to output samples, so the first and last buffers are cut to their audible part. Opus pre-skip that the stream
  configuration already carries is applied by the decoder itself, so a demuxer must not also set it as `trim_start`.
- **Zero copy.** A decoded `AVFrame` is moved into a reference-counted owner stored in `storage`, and the plane pointers
  point into it. Audio buffers are wrapped the same way, planar or interleaved, up to eight planes.
- **Configuration change.** `StreamInfo` carried by `EncodedSample::config_change` is accepted in place when the codec is
  unchanged and the configuration is unchanged, or when it is H.264 or HEVC, whose new parameter sets are passed as
  `NEW_EXTRADATA` side data. A different codec, or a new AAC, Opus or FLAC configuration, makes the write return
  `kUnimplemented`, and the chain recreates the decoder.
- **Errors.** A corrupt sample is a `PortError` with `kDataLoss`, category `kDecode` and hint `kTransient`, and the decoder
  goes on with the next sample. A frame that FFmpeg flags as corrupt is dropped and reported the same way. A pixel or
  sample format that the frame descriptors cannot hold is `kUnimplemented` with category `kCapability` and hint `kFatal`.
  Any other failure is `kRecreate`; after a fatal or recreate error the decoder is silent until the next `Flush`.
  Messages start with their catalogue number (`ALUMBRA-3001`, `ALUMBRA-4001`, `ALUMBRA-4003`), as in
  [codec configuration](codec-config.md).

### Codec configuration

`StreamInfo::codec_config` holds the record that the container carries, and `FfmpegExtradata` turns it into what
libavcodec expects. H.264, HEVC and AAC pass `avcC`, `hvcC` and `AudioSpecificConfig` unchanged. Annex B NAL units are converted to the declared length-prefixed form, and ADTS AAC headers are removed before decode. Vorbis and Theora pass their validated Matroska Xiph-laced headers unchanged. Opus accepts an `OpusHead`or a `dOps` payload, which `umbra::codec::ParseDOps` validates and the port rewrites as an `OpusHead`. FLAC accepts a
34-byte STREAMINFO or a `dfLa` payload, validated by `umbra::codec`. VP8, VP9, AC-3, E-AC-3 and MP3 need none. Annex B H.264 and
HEVC need no configuration at all.

### Frame formats

Software decoders can produce layouts that the frame descriptors do not name. The decoders output 8-bit 4:2:0
(`kI420`), 10-bit 4:2:0 (`kI010`, added to `PixelFormat` for this task) and the semi-planar `kNv12` and `kP010`. They do
not output 4:2:2, 4:4:4, monochrome or 12-bit video until the renderer of #68 can display them. The capability table
therefore accepts H.264 profiles 66, 77, 88, 100 and 110, HEVC profiles 1 to 3, and VP9 profile 0 at 8 bits and profile 2
at 10 bits, instead of every profile FFmpeg can decode.

## Tests

`ffmpeg_decoder_test` decodes the clips of `test/fixtures/decode/` (generated by `generate.sh` with `lavfi` sources
and libx264, libx265, libvpx, libopus, LAME and FFmpeg's own encoders, a few tens of kilobytes each, committed so that
blocking tests need no tool and no network). It covers:

- video output equal to the committed `ffmpeg -f framemd5` references for one, automatic and four threads. H.264, HEVC
  and VP9 are bit-exact, so these are golden hashes (ADR-0021). The references were produced by FFmpeg 8.0.1 and are
  decoder output, which does not depend on the FFmpeg version;
- B-frame reordering, presentation times, durations and the frame sizes and formats;
- audio equal to an in-process reference decode with the same libavcodec, the FLAC output equal to the MD5 inside its
  STREAMINFO, and priming trimmed;
- flush with a new ticket, with no stale frame and the same frames as a reference decode of the tail;
- the grant rules, configuration changes, the unsupported 4:2:2 stream and the factory's candidates and errors;
- corruption: 24 seeded mutations (bit flips, truncation, zero fill of one to four samples) per clip for nine decoders
  (single threaded, because FFmpeg's own frame threading reports a data race under ThreadSanitizer on corrupt H.264 input),
  which must neither crash nor stall, and must report only errors of the documented kinds. Under the `asan` preset this
  catches memory errors in the wrapper and in libavcodec. A libFuzzer harness is not added because the fuzz preset builds
  without the desktop port.

The H.264.1 and H.265.1 conformance bitstreams are not yet in the conformance manifest, so the criterion of matching their
published MD5s is open and tracked as a follow-up.
