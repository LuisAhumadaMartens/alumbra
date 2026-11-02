\file penumbra/interface/port_types.h
Value types of the v1 port contract: player configuration, capability queries, limits and playback reports.

The abstract interfaces that exchange these values are internal and not installed; the
[port contract](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/port-contract.md)
documents each call, its thread and its errors.

\typedef penumbra::SeekTicket
Generation number of a player, carried by writes, events and callbacks.

The first ticket is nonzero and every Seek or Flush carries a strictly larger one. Older tickets are discarded
without effect and future ones are rejected. The ticket never wraps: at exhaustion the player is stopped and
recreated.

\enum penumbra::OutputMode
How a port presents a video stream.

\var penumbra::OutputMode::kTexture
The port returns decoded frames to Umbra for composition.

\var penumbra::OutputMode::kPunchOut
The port presents the video itself in a platform layer that Umbra positions with OutputBounds.

\enum penumbra::InputMode
How Umbra feeds a player, chosen for the whole player.

\var penumbra::InputMode::kSamples
Demuxed access units, as EncodedSample values.

\var penumbra::InputMode::kFragments
Original fMP4 or CMAF fragments, as MediaFragment values, when every stream supports it and no transform is
needed.

\enum penumbra::PlayerState
State reported by a player event.

\var penumbra::PlayerState::kReady
The player is ready to present.

\var penumbra::PlayerState::kPresenting
The player is presenting media.

\var penumbra::PlayerState::kEndOfStream
Output has drained after end of stream: for one stream when the event names a track, otherwise for every stream.

\enum penumbra::RecoveryHint
Recovery that a port suggests with a runtime error.

\var penumbra::RecoveryHint::kTransient
The failure is temporary, and the operation can be retried on the same player.

\var penumbra::RecoveryHint::kRecreate
The player must be stopped and created again.

\var penumbra::RecoveryHint::kFatal
The player cannot recover.

\enum penumbra::PortErrorCategory
Area of a port runtime error.

\var penumbra::PortErrorCategory::kDecode
Decoding failed.

\var penumbra::PortErrorCategory::kOutput
Presenting the output failed.

\var penumbra::PortErrorCategory::kCapability
A capability of the platform changed; usually reported with RecoveryHint::kRecreate.

\var penumbra::PortErrorCategory::kPlatform
Another platform failure.

\enum penumbra::MemoryPressure
Severity of a memory-pressure signal from the platform.

\var penumbra::MemoryPressure::kModerate
Memory is low.

\var penumbra::MemoryPressure::kCritical
Memory is critically low.

\enum penumbra::LogLevel
Severity of a port log message, preserved into Umbra's logging (ADR-0020).

Messages are already redacted: they never contain secrets, URL queries, fragments or user information, or
credential bodies.

\var penumbra::LogLevel::kVerbose
The most detailed level.

\var penumbra::LogLevel::kDebug
Diagnostic detail for developers.

\var penumbra::LogLevel::kInfo
Normal operation.

\var penumbra::LogLevel::kWarning
An unexpected condition.

\var penumbra::LogLevel::kError
A failure.

\struct penumbra::OutputBounds
Position, size and stacking order of a punch-out video layer.

Updates can arrive on every display frame and apply at the next presentation.

\var int32_t penumbra::OutputBounds::x
Horizontal position of the left edge.

\var int32_t penumbra::OutputBounds::y
Vertical position of the top edge.

\var int32_t penumbra::OutputBounds::width
Width; it must not be negative, and zero hides the layer.

\var int32_t penumbra::OutputBounds::height
Height; it must not be negative, and zero hides the layer.

\var int32_t penumbra::OutputBounds::z_order
Stacking order of the layer relative to the platform's other layers.

\struct penumbra::VideoCeiling
Largest video that adaptive bitrate selection will request, passed before the player allocates resources.

Each field is zero when unspecified.

\var int32_t penumbra::VideoCeiling::width
Maximum width in pixels.

\var int32_t penumbra::VideoCeiling::height
Maximum height in pixels.

\var Rational penumbra::VideoCeiling::frame_rate
Maximum frames per second.

\var int64_t penumbra::VideoCeiling::bitrate
Maximum bitrate in bits per second.

\struct penumbra::StreamConfig
Configuration of one stream of a player.

\var StreamInfo penumbra::StreamConfig::info
Description of the stream.

\var std::optional<EncryptionScheme> penumbra::StreamConfig::encryption_scheme
Encryption scheme of the stream, or empty when it is clear.

\var OutputMode penumbra::StreamConfig::output_mode
Presentation mode of a video stream; OutputMode::kTexture by default.

\struct penumbra::PlayerConfig
Configuration from which a port creates a player.

\var int penumbra::PlayerConfig::api_version
Contract version of the caller, which must equal kApiVersion.

\var SeekTicket penumbra::PlayerConfig::initial_ticket
First generation of the player, which must be nonzero; 1 by default.

\var std::vector<StreamConfig> penumbra::PlayerConfig::streams
Configurations of the player's streams, each with a distinct track.

\var InputMode penumbra::PlayerConfig::input_mode
Whether the player receives samples or fragments.

\var std::optional<VideoCeiling> penumbra::PlayerConfig::video_ceiling
Largest video that will be requested, if known.

\struct penumbra::MediaFragment
Original fMP4 or CMAF fragment passed through to a player in fragment mode.

Umbra still parses the fragment for timing and adaptive bitrate selection. A fragment that multiplexes several
tracks is written once, under the grant of one of them.

\var SharedBytes penumbra::MediaFragment::data
Bytes of the fragment, retained without copying once the port accepts it.

\var std::vector<uint32_t> penumbra::MediaFragment::track_ids
Tracks that the fragment contains.

\var MediaTime penumbra::MediaFragment::presentation_time
Parsed presentation time; can be unknown for an initialisation fragment.

\var TimeDelta penumbra::MediaFragment::duration
Parsed duration; can be unknown for an initialisation fragment.

\var bool penumbra::MediaFragment::initialization
Whether this is an initialisation fragment.

\struct penumbra::CapabilityRequest
Decoding capability query, shaped like the web's `MediaCapabilities.decodingInfo()`.

\var StreamInfo penumbra::CapabilityRequest::stream
Stream to decode, including its codec string, geometry and frame rate.

\var std::string penumbra::CapabilityRequest::mime_type
MIME type, including codec parameters.

\var std::string penumbra::CapabilityRequest::key_system
Key system, or empty for clear content.

\var std::optional<EncryptionScheme> penumbra::CapabilityRequest::encryption_scheme
Encryption scheme, or empty for clear content.

\var std::string penumbra::CapabilityRequest::hdr_format
HDR format, or empty for none.

\var int64_t penumbra::CapabilityRequest::bitrate
Bitrate in bits per second.

\struct penumbra::DecodingCapability
Answer to a CapabilityRequest.

Support that a port cannot determine is reported as unsupported.

\var bool penumbra::DecodingCapability::supported
Whether the port can decode the stream.

\var bool penumbra::DecodingCapability::smooth
Whether playback is expected to be smooth.

\var bool penumbra::DecodingCapability::power_efficient
Whether decoding is expected to be power-efficient.

\var OutputMode penumbra::DecodingCapability::preferred_output_mode
Output mode that the port prefers for the stream.

\var bool penumbra::DecodingCapability::accepts_fragments
Whether the port accepts the stream as passed-through fragments.

\struct penumbra::PortLimits
Limits of a port.

\var int penumbra::PortLimits::api_version
Contract version of the port, which must equal kApiVersion.

\var size_t penumbra::PortLimits::max_players
Maximum number of concurrent players, at least 1.

\struct penumbra::FrameCounters
Video frame counters of a player, reset with each generation.

\var uint64_t penumbra::FrameCounters::decoded
Frames decoded.

\var uint64_t penumbra::FrameCounters::dropped
Frames dropped instead of presented.

\var uint64_t penumbra::FrameCounters::corrupted
Frames decoded with errors.

\struct penumbra::PlaybackSnapshot
Media clock, audio latency and frame counters of a player at one moment.

\var MediaTime penumbra::PlaybackSnapshot::media_time
Current media time; unknown when not available.

\var TimeDelta penumbra::PlaybackSnapshot::audio_output_latency
Latency of the local or remote audio output; unknown when not available.

\var FrameCounters penumbra::PlaybackSnapshot::frames
Video frame counters.
