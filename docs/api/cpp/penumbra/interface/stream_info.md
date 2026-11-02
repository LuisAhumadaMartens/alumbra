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
