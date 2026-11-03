\file penumbra/interface/stream_info.h
Descriptions of the video, audio and text tracks that Umbra passes to a port.

\enum penumbra::StreamType
Kind of track, derived from StreamInfo::format.

\var penumbra::StreamType::kVideo
A video track.

\var penumbra::StreamType::kAudio
An audio track.

\var penumbra::StreamType::kText
A text track.

\enum penumbra::CodecId
Codec of a track.

The profile and level are in the RFC 6381 codec string, StreamInfo::codec_string.

\var penumbra::CodecId::kUnknown
Codec not identified.

\var penumbra::CodecId::kH264
H.264, also called AVC.

\var penumbra::CodecId::kHevc
H.265, also called HEVC.

\var penumbra::CodecId::kVp9
VP9.

\var penumbra::CodecId::kAv1
AV1.

\var penumbra::CodecId::kAac
AAC.

\var penumbra::CodecId::kOpus
Opus.

\var penumbra::CodecId::kAc3
Dolby Digital, also called AC-3.

\var penumbra::CodecId::kEac3
Dolby Digital Plus, also called E-AC-3.

\var penumbra::CodecId::kFlac
FLAC.

\var penumbra::CodecId::kMp3
MPEG audio layer III, also called MP3.

\var penumbra::CodecId::kVp8
VP8 video.

\var penumbra::CodecId::kVorbis
Vorbis audio.

\var penumbra::CodecId::kTheora
Theora video.

\enum penumbra::NalUnitFraming
Packetisation of H.264 and HEVC NAL units in encoded samples.

\var penumbra::NalUnitFraming::kUnspecified
NAL unit packetisation is not specified.

\var penumbra::NalUnitFraming::kAnnexB
NAL units are separated by Annex B start codes.

\var penumbra::NalUnitFraming::kLengthPrefixed
Each NAL unit is preceded by a big-endian length field. `nal_length_size` gives its size in bytes.

\enum penumbra::AacFraming
Packetisation of AAC data in encoded samples.

\var penumbra::AacFraming::kUnspecified
AAC packetisation is not specified.

\var penumbra::AacFraming::kRaw
Samples contain AAC access units without ADTS headers.

\var penumbra::AacFraming::kAdts
Samples contain an ADTS header followed by AAC data.
\enum penumbra::SampleFormat
Layout of decoded audio samples.

Interleaved formats store the channels of each sample frame together; planar formats store one plane per
channel.

\var penumbra::SampleFormat::kUnknown
Format not identified.

\var penumbra::SampleFormat::kU8
Interleaved unsigned 8-bit samples.

\var penumbra::SampleFormat::kS16
Interleaved signed 16-bit samples.

\var penumbra::SampleFormat::kS32
Interleaved signed 32-bit samples.

\var penumbra::SampleFormat::kF32
Interleaved 32-bit floating-point samples.

\var penumbra::SampleFormat::kU8Planar
Planar unsigned 8-bit samples.

\var penumbra::SampleFormat::kS16Planar
Planar signed 16-bit samples.

\var penumbra::SampleFormat::kS32Planar
Planar signed 32-bit samples.

\var penumbra::SampleFormat::kF32Planar
Planar 32-bit floating-point samples.

\enum penumbra::ChromaSiting
Position of subsampled chroma samples relative to the luma samples of a 4:2:0 frame.

\var penumbra::ChromaSiting::kUnspecified
Siting not signalled; renderers treat it as kLeft, the MPEG-2 and H.264 default.

\var penumbra::ChromaSiting::kLeft
Chroma is horizontally aligned with the left luma sample of each pair and vertically centred between rows.

\var penumbra::ChromaSiting::kCenter
Chroma is centred between the luma samples both horizontally and vertically, as in MPEG-1 and JPEG.

\var penumbra::ChromaSiting::kTopLeft
Chroma is aligned with the top-left luma sample of each 2x2 block.

\struct penumbra::ColorSpace
Colour description of a video track, using ITU-T H.273 code points.

It is a placeholder that HDR static metadata will extend.

\var static constexpr uint8_t penumbra::ColorSpace::kUnspecified
H.273 code point 2, unspecified, the default of each code point field.

\var uint8_t penumbra::ColorSpace::primaries
Colour primaries code point.

\var uint8_t penumbra::ColorSpace::transfer
Transfer characteristics code point.

\var uint8_t penumbra::ColorSpace::matrix
Matrix coefficients code point.

\var bool penumbra::ColorSpace::full_range
Whether samples use the full range instead of the limited video range.

\var ChromaSiting penumbra::ColorSpace::chroma_siting
Where subsampled chroma samples sit relative to luma; kUnspecified by default.

\fn constexpr bool penumbra::ColorSpace::operator==(const ColorSpace&, const ColorSpace&)
Compares every field.

\struct penumbra::VideoInfo
Geometry, frame rate and colour of a video track.

\var int32_t penumbra::VideoInfo::coded_width
Width of the coded picture in pixels.

\var int32_t penumbra::VideoInfo::coded_height
Height of the coded picture in pixels.

\var int32_t penumbra::VideoInfo::display_width
Width of the displayed picture in pixels.

\var int32_t penumbra::VideoInfo::display_height
Height of the displayed picture in pixels.

\var Rational penumbra::VideoInfo::sample_aspect_ratio
Sample aspect ratio; `1/1` by default.

\var Rational penumbra::VideoInfo::frame_rate
Frames per second; `0/1` when unknown.

\var ColorSpace penumbra::VideoInfo::color
Colour description.

\struct penumbra::AudioInfo
Sample rate, channels and sample format of an audio track.

\var int32_t penumbra::AudioInfo::sample_rate
Sample frames per second.

\var int32_t penumbra::AudioInfo::channel_count
Number of channels.

\var uint64_t penumbra::AudioInfo::channel_mask
Channel layout as WAVE and FFmpeg channel bits; 0 when unspecified.

\var SampleFormat penumbra::AudioInfo::sample_format
Layout of decoded samples.

\struct penumbra::TextInfo
Placeholder description of a text track, until D-15 defines the cue model.

\struct penumbra::StreamInfo
Description of one track: codec, codec configuration, time base, language and format.

\var uint32_t penumbra::StreamInfo::track_id
Identifier that samples and port calls use to refer to the track.

\var CodecId penumbra::StreamInfo::codec
Codec of the track.

\var std::string penumbra::StreamInfo::codec_string
RFC 6381 codec string, for example `avc1.64001f`.

\var SharedBytes penumbra::StreamInfo::codec_config
Codec configuration bytes, for example an `avcC` record.

\var NalUnitFraming penumbra::StreamInfo::nal_unit_framing
NAL unit packetisation for H.264 and HEVC samples.

\var uint8_t penumbra::StreamInfo::nal_length_size
Length field size in bytes for the length-prefixed representation described by the codec configuration; zero when
the configuration does not supply a length size.

\var AacFraming penumbra::StreamInfo::aac_framing
AAC packetisation when the codec is AAC.

\var Rational penumbra::StreamInfo::time_base
Time base of the timestamps of the track's samples.

\var std::string penumbra::StreamInfo::language
BCP 47 language tag.

\var std::variant<VideoInfo, AudioInfo, TextInfo> penumbra::StreamInfo::format
Track-specific description; VideoInfo by default.

\fn StreamType penumbra::StreamInfo::type() const
Returns the kind of track that `format` describes.

\fn constexpr std::string_view penumbra::StreamTypeName(StreamType type)
Returns `video`, `audio` or `text`, or `unknown` for an unlisted value.

\fn constexpr std::string_view penumbra::CodecName(CodecId codec)
Returns the lower-case codec name, such as `h264` or `eac3`, or `unknown`.

\fn template <typename Sink> void penumbra::AbslStringify(Sink& sink, StreamType type)
Lets Abseil formatting and logging print `type` as StreamTypeName() does.

\fn template <typename Sink> void penumbra::AbslStringify(Sink& sink, CodecId codec)
Lets Abseil formatting and logging print `codec` as CodecName() does.
