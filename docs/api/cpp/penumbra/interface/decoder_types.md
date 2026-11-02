\file penumbra/interface/decoder_types.h
Value types that describe decoders and the settings that choose between them.

A port lists its decoders as DecoderCandidate values and Umbra passes the resolved DecoderSettings in
PlayerConfig::decoder. The reference player filters and orders the candidates and falls back between them; the
[decoder chain](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/decoder-chain.md)
describes the rules.

\enum penumbra::DecoderKind
Class of a decoder, which fixes its rank in the candidate order.

\var penumbra::DecoderKind::kHardware
A hardware decoder; tried first.

\var penumbra::DecoderKind::kPlatformSoftware
A software decoder that the platform provides, such as Android's own software codecs; tried second.

\var penumbra::DecoderKind::kBundledSoftware
A software decoder that Alumbra ships, such as libavcodec or dav1d; tried last.

\enum penumbra::DecoderOutput
Kind of storage in which a decoder produces video frames.

\var penumbra::DecoderOutput::kCpuBuffer
Frames in system memory.

\var penumbra::DecoderOutput::kGpuSurface
Frames in port-owned GPU surfaces, described as PixelFormat::kOpaque.

\enum penumbra::HardwarePreference
Value of the setting `decode.prefer_hardware`.

\var penumbra::HardwarePreference::kAuto
Hardware decoders first, then software decoders.

\var penumbra::HardwarePreference::kAlways
Only hardware decoders, so a hardware failure is reported instead of falling back to software.

\var penumbra::HardwarePreference::kNever
Only software decoders.

\struct penumbra::DecoderCandidate
A decoder that a port can create, as the port describes it.

\var std::string penumbra::DecoderCandidate::name
Stable decoder name, used by the blocklist, the overrides and diagnostics.

\var DecoderKind penumbra::DecoderCandidate::kind
Class of the decoder.

\var DecoderOutput penumbra::DecoderCandidate::output
Storage of the frames the decoder produces.

\var bool penumbra::DecoderCandidate::secure
Whether the decoder can decode protected content on a secure path.

\var std::vector<CodecId> penumbra::DecoderCandidate::codecs
Codecs that the decoder supports.

\var int32_t penumbra::DecoderCandidate::max_width
Largest supported coded width in pixels; zero means no limit.

\var int32_t penumbra::DecoderCandidate::max_height
Largest supported coded height in pixels; zero means no limit.

\var std::vector<std::string> penumbra::DecoderCandidate::quirks
Port-defined tags for workarounds that apply to this decoder.

\struct penumbra::DecoderSettings
Resolved decoder settings that Umbra passes to the port.

The defaults equal the defaults of the corresponding `decode.*` settings.

\var HardwarePreference penumbra::DecoderSettings::prefer_hardware
Value of `decode.prefer_hardware`.

\var bool penumbra::DecoderSettings::fallback_enabled
Value of `decode.fallback.enabled`; when false the chain never moves to another decoder.

\var int32_t penumbra::DecoderSettings::dropped_frames_max_percent
Value of `decode.dropped_frames.max_percent`: the share of due frames that may drop decode-late in one window.

\var int32_t penumbra::DecoderSettings::dropped_frames_window_ms
Value of `decode.dropped_frames.window_ms`: the length of the drop window in milliseconds.

\var std::vector<std::string> penumbra::DecoderSettings::preferred_decoders
Value of `decode.video.preferred_decoders`: decoder names that move to the front of the filtered candidates. It
never restores a blocked decoder.

\var std::vector<std::string> penumbra::DecoderSettings::blocklist_extra_decoders
Value of `decode.blocklist.extra_decoders`: decoder names, each optionally followed by `:` and a codec name, to
exclude.

\var std::vector<std::string> penumbra::DecoderSettings::blocklist_ignored_entries
Value of `decode.blocklist.ignored_entries`: IDs of built-in blocklist entries that are not applied.

\fn constexpr std::string_view penumbra::DecoderKindName(DecoderKind kind)
Returns the lowercase name of a decoder kind, or `unknown` for a value outside the enumeration.

\fn constexpr std::string_view penumbra::DecoderOutputName(DecoderOutput output)
Returns the lowercase name of a decoder output, or `unknown` for a value outside the enumeration.
