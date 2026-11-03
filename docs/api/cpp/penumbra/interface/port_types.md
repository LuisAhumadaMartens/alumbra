\file penumbra/interface/port_types.h
Value types of the port contract: player configuration, capability queries, limits and playback reports.

Caption accessibility settings are optional snapshots. An unset field means the port has no active preference for
that field, so Umbra can retain author styling.

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

\enum penumbra::CaptionFontFamily
Platform-independent caption font family choices exposed by accessibility preferences.

\var penumbra::CaptionFontFamily::kDefault
The platform's default caption font.

\var penumbra::CaptionFontFamily::kMonospaceSerif
A monospaced serif font.

\var penumbra::CaptionFontFamily::kProportionalSerif
A proportional serif font.

\var penumbra::CaptionFontFamily::kMonospaceSans
A monospaced sans-serif font.

\var penumbra::CaptionFontFamily::kProportionalSans
A proportional sans-serif font.

\var penumbra::CaptionFontFamily::kCasual
A casual display font.

\var penumbra::CaptionFontFamily::kCursive
A cursive font.

\var penumbra::CaptionFontFamily::kSmallCaps
A small-capitals font.

\enum penumbra::CaptionEdgeStyle
Caption text edge effects exposed by accessibility preferences.

\var penumbra::CaptionEdgeStyle::kNone
No text edge effect.

\var penumbra::CaptionEdgeStyle::kRaised
A raised text edge.

\var penumbra::CaptionEdgeStyle::kDepressed
A depressed text edge.

\var penumbra::CaptionEdgeStyle::kUniform
A uniform text outline.

\var penumbra::CaptionEdgeStyle::kDropShadow
A drop shadow.

\struct penumbra::CaptionColor
An RGB caption colour with 8-bit channels.

\var penumbra::CaptionColor::red
Red channel.

\var penumbra::CaptionColor::green
Green channel.

\var penumbra::CaptionColor::blue
Blue channel.

\struct penumbra::CaptionPreferences
Current platform caption preferences. Opacity values are finite fractions from zero to one, and position offset is a
percentage of the caption safe area.

\var penumbra::CaptionPreferences::enabled
Whether the platform requests captions to be presented.

\var penumbra::CaptionPreferences::font_family
Preferred platform caption font family.

\var penumbra::CaptionPreferences::font_size_scale
Preferred caption font scale relative to the platform's normal size.

\var penumbra::CaptionPreferences::text_color
Preferred caption foreground colour.

\var penumbra::CaptionPreferences::text_opacity
Preferred caption foreground opacity.

\var penumbra::CaptionPreferences::background_color
Preferred caption background colour.

\var penumbra::CaptionPreferences::background_opacity
Preferred caption background opacity.

\var penumbra::CaptionPreferences::window_color
Preferred caption window colour.

\var penumbra::CaptionPreferences::window_opacity
Preferred caption window opacity.

\var penumbra::CaptionPreferences::edge_style
Preferred caption text edge effect.

\var penumbra::CaptionPreferences::position_offset_percent
Preferred vertical position offset in percent.

\var penumbra::MemoryPressure::kNormal
Memory pressure has cleared.

\var penumbra::MemoryPressure::kModerate
Memory is low.

\var penumbra::MemoryPressure::kCritical
Memory is critically low.

\struct penumbra::BufferBudgets
Optional port-provided byte limits for retained audio and video samples. An absent value leaves the configured
Umbra budget in effect.

\var std::optional<uint64_t> penumbra::BufferBudgets::video_max_bytes
Optional maximum bytes for video samples.

\var std::optional<uint64_t> penumbra::BufferBudgets::audio_max_bytes
Optional maximum bytes for audio samples.

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

\var DecoderSettings penumbra::PlayerConfig::decoder
Resolved decoder settings, which the reference player uses to order candidates and to fall back.

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
