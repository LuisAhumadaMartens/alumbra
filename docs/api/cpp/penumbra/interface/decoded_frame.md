\file penumbra/interface/decoded_frame.h
Descriptors of decoded video frames and audio buffers in memory that the port owns.

The reference player's queues move these descriptors, never pixels or samples.

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

\var SampleFormat penumbra::DecodedAudioBuffer::format
Layout of the samples.

\var std::array<const uint8_t*, kMaxPlanes> penumbra::DecodedAudioBuffer::planes
One pointer for an interleaved format, or one per channel for a planar format; unused entries are null.

\var std::shared_ptr<const void> penumbra::DecodedAudioBuffer::storage
Owner of the memory that the planes point into.
