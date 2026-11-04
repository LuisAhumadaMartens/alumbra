\file umbra/player/observability.h
Typed per-player log and trace records.

\enum umbra::player::LogLevel
Log severity levels shared by structured log records and the generated observability settings.

\var umbra::player::LogLevel::kVerbose
Detailed diagnostics.

\var umbra::player::LogLevel::kDebug
Debugging diagnostics.

\var umbra::player::LogLevel::kInfo
Informational records.

\var umbra::player::LogLevel::kWarning
Degraded behaviour records.

\var umbra::player::LogLevel::kError
Failure records.

\enum umbra::player::TraceCategory
The shared ADR-0020 categories used by logs and traces.

\var umbra::player::TraceCategory::kNet
Network requests and transport.

\var umbra::player::TraceCategory::kManifest
Manifest parsing and updates.

\var umbra::player::TraceCategory::kAbr
Adaptive bitrate selection.

\var umbra::player::TraceCategory::kBuffer
Buffering decisions.

\var umbra::player::TraceCategory::kDemux
Media demultiplexing.

\var umbra::player::TraceCategory::kDecode
Decoder operation.

\var umbra::player::TraceCategory::kRender
Video presentation.

\var umbra::player::TraceCategory::kAudio
Audio playback.

\var umbra::player::TraceCategory::kText
Captions and subtitles.

\var umbra::player::TraceCategory::kAds
Advertisement playback.

\var umbra::player::TraceCategory::kDrm
Content protection.

\var umbra::player::TraceCategory::kSettings
Configuration changes.

\var umbra::player::TraceCategory::kPlayer
Player lifecycle and state.

\var umbra::player::TraceCategory::kPort
Platform integration.

\enum umbra::player::TracePhase
Trace event phases.

\var umbra::player::TracePhase::kBegin
Begins a duration event.

\var umbra::player::TracePhase::kEnd
Ends a duration event.

\var umbra::player::TracePhase::kInstant
Records an instantaneous event.

\var umbra::player::TracePhase::kCounter
Records a counter value.

\struct umbra::player::LogRecord
A structured log record delivered to the owning player's LogSink.

\var umbra::player::LogRecord::category
Typed category for the record.

\var umbra::player::LogRecord::level
Severity of the record.

\var umbra::player::LogRecord::message
Human-readable message.

\var umbra::player::LogRecord::fields
Structured fields with lower-snake-case names and setting-compatible units.

\struct umbra::player::TraceRecord
A trace event delivered to the owning player's TraceSink.

\var umbra::player::TraceRecord::category
Typed category for the event.

\var umbra::player::TraceRecord::phase
Trace phase.

\var umbra::player::TraceRecord::name
Event name.

\var umbra::player::TraceRecord::value
Optional integer value for counter events.

\typedef umbra::player::LogSink
Callback receiving log records from one player.

\typedef umbra::player::TraceSink
Callback receiving trace records from one player.
