\file penumbra/interface/decoded_frame.h
Descriptors of decoded video frames and audio buffers in memory that the port owns.

The reference player's queues move these descriptors, never pixels or samples.

\enum penumbra::AudioChannel
Named audio channel position used when a channel layout is not represented by a native mask.

\var penumbra::AudioChannel::kUnknown
Channel position is unavailable.

\var penumbra::AudioChannel::kFrontLeft
Front left channel.

\var penumbra::AudioChannel::kFrontRight
Front right channel.

\var penumbra::AudioChannel::kFrontCenter
Front centre channel.

\var penumbra::AudioChannel::kLowFrequency
Low-frequency effects channel.

\var penumbra::AudioChannel::kBackLeft
Back left channel.

\var penumbra::AudioChannel::kBackRight
Back right channel.

\var penumbra::AudioChannel::kFrontLeftOfCenter
Front left-of-centre channel.

\var penumbra::AudioChannel::kFrontRightOfCenter
Front right-of-centre channel.

\var penumbra::AudioChannel::kBackCenter
Back centre channel.

\var penumbra::AudioChannel::kSideLeft
Side left channel.

\var penumbra::AudioChannel::kSideRight
Side right channel.

\var penumbra::AudioChannel::kTopCenter
Top centre channel.

\var penumbra::AudioChannel::kTopFrontLeft
Top front left channel.

\var penumbra::AudioChannel::kTopFrontCenter
Top front centre channel.

\var penumbra::AudioChannel::kTopFrontRight
Top front right channel.

\var penumbra::AudioChannel::kTopBackLeft
Top back left channel.

\var penumbra::AudioChannel::kTopBackCenter
Top back centre channel.

\var penumbra::AudioChannel::kTopBackRight
Top back right channel.

\var penumbra::AudioChannel::kWideLeft
Wide left channel.

\var penumbra::AudioChannel::kWideRight
Wide right channel.

\var penumbra::AudioChannel::kSurroundDirectLeft
Surround direct left channel.

\var penumbra::AudioChannel::kSurroundDirectRight
Surround direct right channel.

\var penumbra::AudioChannel::kLowFrequency2
Second low-frequency effects channel.

\var penumbra::AudioChannel::kTopSideLeft
Top side left channel.

\var penumbra::AudioChannel::kTopSideRight
Top side right channel.

\var penumbra::AudioChannel::kBottomFrontCenter
Bottom front centre channel.

\var penumbra::AudioChannel::kBottomFrontLeft
Bottom front left channel.

\var penumbra::AudioChannel::kBottomFrontRight
Bottom front right channel.

\enum penumbra::PixelFormat
Memory layout of a decoded video frame.

\var penumbra::PixelFormat::kUnknown
Format not identified.

\var penumbra::PixelFormat::kI420
8-bit YUV 4:2:0 with separate Y, U and V planes.

\var penumbra::PixelFormat::kNv12
8-bit YUV 4:2:0 with a Y plane and an interleaved UV plane.

\var penumbra::PixelFormat::kP010
10-bit YUV 4:2:0 in 16-bit words, with a Y plane and an interleaved UV plane.

\var penumbra::PixelFormat::kOpaque
A hardware or platform frame without CPU-accessible planes.

\var penumbra::PixelFormat::kI010
10-bit YUV 4:2:0 in 16-bit little-endian words, with separate Y, U and V planes.

\struct penumbra::VideoPlane
One plane of a decoded video frame.

\var const uint8_t* penumbra::VideoPlane::data
First byte of the plane, kept alive by the frame's storage.

\var int32_t penumbra::VideoPlane::stride
Number of bytes from the start of one row to the start of the next.

\struct penumbra::DecodedVideoFrame
Timing, geometry and planes of one decoded video frame in port-owned memory.

`storage` keeps the port's buffer, surface or platform object alive for as long as any copy of the descriptor
exists.

\var static constexpr size_t penumbra::DecodedVideoFrame::kMaxPlanes
Maximum number of planes.

\fn penumbra::DecodedVideoFrame::DecodedVideoFrame()
Creates an empty descriptor with unknown timing, no planes and no storage.

\fn penumbra::DecodedVideoFrame::DecodedVideoFrame(const DecodedVideoFrame&)
Copies the descriptor; the copies share `storage`, so the planes stay valid while either exists.

\fn DecodedVideoFrame& penumbra::DecodedVideoFrame::operator=(const DecodedVideoFrame&)
Copies the descriptor and shares `storage`, releasing this descriptor's reference to its previous storage.

\fn penumbra::DecodedVideoFrame::DecodedVideoFrame(DecodedVideoFrame&& other) noexcept
Takes over the frame of `other` and resets `other` to the default, empty descriptor.

A moved-from frame never keeps plane pointers into memory it no longer owns.

\fn DecodedVideoFrame& penumbra::DecodedVideoFrame::operator=(DecodedVideoFrame&& other) noexcept
Replaces this frame with the frame of `other` and resets `other` to the default, empty descriptor.

\var MediaTime penumbra::DecodedVideoFrame::presentation_time
Presentation time; unknown by default.

\var TimeDelta penumbra::DecodedVideoFrame::duration
Display duration; unknown by default.

\var int32_t penumbra::DecodedVideoFrame::width
Width in pixels.

\var int32_t penumbra::DecodedVideoFrame::height
Height in pixels.

\var PixelFormat penumbra::DecodedVideoFrame::format
Memory layout of the planes.

\var std::array<VideoPlane, kMaxPlanes> penumbra::DecodedVideoFrame::planes
Plane pointers and strides; unused planes, and every plane of a PixelFormat::kOpaque frame, are null.

\var ColorSpace penumbra::DecodedVideoFrame::color
Colour description.

\var std::shared_ptr<const void> penumbra::DecodedVideoFrame::storage
Owner of the memory that the planes point into.

\struct penumbra::DecodedAudioBuffer
Timing, format and planes of decoded audio in port-owned memory.

`storage` keeps the port's buffer alive for as long as any copy of the descriptor exists.

\var static constexpr size_t penumbra::DecodedAudioBuffer::kMaxPlanes
Maximum number of planes.

\fn penumbra::DecodedAudioBuffer::DecodedAudioBuffer()
Creates an empty descriptor with unknown timing, no planes and no storage.

\fn penumbra::DecodedAudioBuffer::DecodedAudioBuffer(const DecodedAudioBuffer&)
Copies the descriptor; the copies share `storage`, so the planes stay valid while either exists.

\fn DecodedAudioBuffer& penumbra::DecodedAudioBuffer::operator=(const DecodedAudioBuffer&)
Copies the descriptor and shares `storage`, releasing this descriptor's reference to its previous storage.

\fn penumbra::DecodedAudioBuffer::DecodedAudioBuffer(DecodedAudioBuffer&& other) noexcept
Takes over the buffer of `other` and resets `other` to the default, empty descriptor.

\fn DecodedAudioBuffer& penumbra::DecodedAudioBuffer::operator=(DecodedAudioBuffer&& other) noexcept
Replaces this buffer with the buffer of `other` and resets `other` to the default, empty descriptor.

\var MediaTime penumbra::DecodedAudioBuffer::presentation_time
Presentation time of the first sample frame; unknown by default.

\var int32_t penumbra::DecodedAudioBuffer::frame_count
Number of sample frames, each holding one sample per channel.

\var int32_t penumbra::DecodedAudioBuffer::sample_rate
Sample frames per second.

\var int32_t penumbra::DecodedAudioBuffer::channel_count
Number of channels.

\var uint64_t penumbra::DecodedAudioBuffer::channel_mask
Native channel layout mask when the port can describe it, or zero when the layout is unspecified.

\var std::array<AudioChannel, kMaxPlanes> penumbra::DecodedAudioBuffer::channel_layout
Channel positions in sample order when no native mask describes the layout.

\var bool penumbra::DecodedAudioBuffer::has_downmix_metadata
Whether the decoder supplied bitstream downmix coefficients.

\var float penumbra::DecodedAudioBuffer::center_mix_level
Centre-channel coefficient supplied by downmix metadata, or the ITU-R BS.775 default.

\var float penumbra::DecodedAudioBuffer::surround_mix_level
Surround-channel coefficient supplied by downmix metadata, or the ITU-R BS.775 default.

\var float penumbra::DecodedAudioBuffer::lfe_mix_level
LFE coefficient supplied by downmix metadata, or zero by default.

\var SampleFormat penumbra::DecodedAudioBuffer::format
Layout of the samples.

\var std::array<const uint8_t*, kMaxPlanes> penumbra::DecodedAudioBuffer::planes
One pointer for an interleaved format, or one per channel for a planar format; unused entries are null.

\var std::shared_ptr<const void> penumbra::DecodedAudioBuffer::storage
Owner of the memory that the planes point into.
