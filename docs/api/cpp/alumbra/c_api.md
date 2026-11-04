\file umbra/c_api/c_api.h

# C ABI

**Stability: experimental.** The C ABI may change in any release before 1.0.

The installed `umbra/c_api/c_api.h` header is valid C11 and C++. Exported names
begin with `alumbra_`. Call `alumbra_negotiate_version` before any other entry
point.

Every extensible structure starts with `struct_size`. Set it to the size of the
structure supplied by the caller. A callee checks the fields it uses and does
not read beyond the supplied size. New fields are appended. Initialise
structures to zero before setting fields.

Player handles are unsigned identifiers, not addresses. Their low 32 bits
identify a registry slot and their high 32 bits hold its generation. Destroying
a player advances the slot generation before reuse. Zero and stale handles
return `ALUMBRA_STATUS_INVALID_HANDLE`. Each handle has one owner: the thread
that created it. All calls for that handle, including destruction and event
polling, must run on that thread. A wrong-thread call returns
`ALUMBRA_STATUS_WRONG_THREAD` without accessing player state.

The player owns its settings store and event queue. The notify hook carries no
payload and may run on any engine thread; it should only schedule a drain on the
owner thread. Polling dispatches queued C++ player events and returns their C
payloads in order. Event sequence numbers increase for each event. Destroying a
player releases its listeners, settings and queued events.

Strings are pointer and byte-count pairs. The byte count excludes a trailing
zero byte, and input strings need not be zero terminated. The caller retains
input storage for the duration of the call. Output buffers report the required
byte count through their size pointer and do not append a zero byte. A null
output buffer with zero capacity queries the required size.

Settings are read from the effective value and written to the user layer.
Values use the canonical schema text representation. Unknown keys and invalid
values return `ALUMBRA_STATUS_INVALID_ARGUMENT`.

Text-track handles are opaque, generation-checked identifiers owned by their
player. Release each acquired handle with `alumbra_text_track_release`. A track
handle becomes invalid when its player is destroyed. Media sample bytes remain
owned by the WASM output pipeline; the JavaScript transfer helper passes a view
to browser APIs without making an intermediate copy.

\def ALUMBRA_C_API_VERSION
The current C ABI revision.

\typedef alumbra_handle
An opaque, generation-checked player identifier.

\typedef alumbra_track_handle
An opaque, generation-checked text-track identifier.

\typedef alumbra_status
Result codes returned by ABI calls.

\struct alumbra_version_info
Version negotiation input and output.

\typedef alumbra_version_info
The version negotiation structure.

\var alumbra_version_info::struct_size
The size in bytes of the structure supplied by the caller.

\var alumbra_version_info::requested_version
The ABI revision requested by the caller.

\var alumbra_version_info::supported_version
The ABI revision implemented by the library.

\struct alumbra_player_config
Player creation options.

\typedef alumbra_player_config
The player configuration structure.

\var alumbra_player_config::struct_size
The size in bytes of the configuration structure.

\var alumbra_player_config::notify
The payload-free event availability hook.

\var alumbra_player_config::user_data
The embedder context passed to the notification hook.

\struct alumbra_event
One event pulled from a player queue.

\typedef alumbra_event
The event structure returned by polling.

\var alumbra_event::struct_size
The size in bytes of the event structure.

\var alumbra_event::type
The event type identifier.

\var alumbra_event::sequence
The event ordering sequence number.

\var alumbra_event::current_time
The playback time captured when the event was queued, in seconds.

\var alumbra_event::duration
The media duration captured when the event was queued, in seconds.

\var alumbra_event::error_code
The HTML MediaError code for error and warning events, or zero otherwise.

\struct alumbra_error_info
The HTML media error code and the message size.

\typedef alumbra_error_info
The error output structure.

\var alumbra_error_info::struct_size
The size in bytes of the error structure.

\var alumbra_error_info::code
The HTML media error code, or zero when no error is present.

\var alumbra_error_info::message_size
The required message size in bytes, excluding a trailing zero byte.

\struct alumbra_statistics
The per-player statistics snapshot and its available-value mask.

\typedef alumbra_statistics
The statistics snapshot structure.

\var alumbra_statistics::struct_size
The size in bytes of the statistics structure supplied by the caller.

\var alumbra_statistics::reserved
Reserved for future use and set to zero.

\var alumbra_statistics::present_mask
The bit mask identifying statistics with available values.

\var alumbra_statistics::average_initial_startup_time_s
The average initial startup time in seconds.

\var alumbra_statistics::startup_failures
The number of startup failures.

\var alumbra_statistics::playback_failure_percentage
The percentage of playback sessions that failed.

\var alumbra_statistics::average_playback_stalled_count
The average number of stalls per playback session.

\var alumbra_statistics::average_stalled_time_percentage
The percentage of watched time spent stalled.

\var alumbra_statistics::average_playback_bitrate_kbps
The average playback bitrate in kilobits per second.

\var alumbra_statistics::average_segment_bitrate_bps
The average segment bitrate in bits per second.

\var alumbra_statistics::rendition_switch_count
The number of rendition switches.

\var alumbra_statistics::dropped_frames
The number of dropped video frames.

\var alumbra_statistics::live_latency_ms
The latest live latency in milliseconds.

\def ALUMBRA_STATUS_OK
The call succeeded.

\def ALUMBRA_STATUS_INVALID_ARGUMENT
The caller supplied an invalid argument, structure size or setting value.

\def ALUMBRA_STATUS_INVALID_HANDLE
The handle is zero, stale or does not identify a live player.

\def ALUMBRA_STATUS_UNSUPPORTED
The requested ABI revision or operation is not available.

\def ALUMBRA_STATUS_BUFFER_TOO_SMALL
The output buffer does not have the required capacity.

\def ALUMBRA_STATUS_WRONG_THREAD
The call was made from a thread other than the player's owner thread.

\def ALUMBRA_EVENT_LOADSTART
The media load algorithm started.

\def ALUMBRA_EVENT_PROGRESS
The player received media data.

\def ALUMBRA_EVENT_SUSPEND
The player suspended resource fetching.

\def ALUMBRA_EVENT_ABORT
The media load was aborted.

\def ALUMBRA_EVENT_ERROR
A fatal media error occurred.

\def ALUMBRA_EVENT_EMPTIED
The media element was emptied.

\def ALUMBRA_EVENT_STALLED
Fetching media data stalled.

\def ALUMBRA_EVENT_LOADEDMETADATA
Media metadata became available.

\def ALUMBRA_EVENT_LOADEDDATA
The first media data became available.

\def ALUMBRA_EVENT_CANPLAY
Playback can begin.

\def ALUMBRA_EVENT_CANPLAYTHROUGH
Playback can continue to the end without buffering.

\def ALUMBRA_EVENT_PLAYING
Playback started or resumed.

\def ALUMBRA_EVENT_WAITING
Playback is waiting for media data.

\def ALUMBRA_EVENT_SEEKING
A seek started.

\def ALUMBRA_EVENT_SEEKED
A seek completed.

\def ALUMBRA_EVENT_ENDED
Playback reached the end.

\def ALUMBRA_EVENT_DURATIONCHANGE
The media duration changed.

\def ALUMBRA_EVENT_TIMEUPDATE
The playback position changed.

\def ALUMBRA_EVENT_PLAY
A play request started.

\def ALUMBRA_EVENT_PAUSE
Playback was paused.

\def ALUMBRA_EVENT_RATECHANGE
The playback rate changed.

\def ALUMBRA_EVENT_RESIZE
The video dimensions changed.

\def ALUMBRA_EVENT_VOLUMECHANGE
The volume or muted state changed.

\def ALUMBRA_EVENT_WARNING
A degraded media error was reported.

\def ALUMBRA_TEXT_TRACK_SUBTITLES
Subtitle text-track kind.

\def ALUMBRA_TEXT_TRACK_CAPTIONS
Caption text-track kind.

\def ALUMBRA_TEXT_TRACK_DESCRIPTIONS
Audio-description text-track kind.

\def ALUMBRA_TEXT_TRACK_CHAPTERS
Chapter text-track kind.

\def ALUMBRA_TEXT_TRACK_METADATA
Metadata text-track kind.

\def ALUMBRA_TEXT_TRACK_DISABLED
Disabled text-track mode.

\def ALUMBRA_TEXT_TRACK_HIDDEN
Hidden text-track mode.

\def ALUMBRA_TEXT_TRACK_SHOWING
Showing text-track mode.

\def ALUMBRA_PRELOAD_NONE
Do not preload media data.

\def ALUMBRA_PRELOAD_METADATA
Preload metadata only.

\def ALUMBRA_PRELOAD_AUTO
Allow automatic preloading.

\def ALUMBRA_TIME_RANGES_BUFFERED
Select the buffered time ranges.

\def ALUMBRA_TIME_RANGES_SEEKABLE
Select the seekable time ranges.

\def ALUMBRA_STATISTIC_AVERAGE_INITIAL_STARTUP_TIME
Select average initial startup time in the statistics presence mask.

\def ALUMBRA_STATISTIC_STARTUP_FAILURES
Select startup failures in the statistics presence mask.

\def ALUMBRA_STATISTIC_PLAYBACK_FAILURE_PERCENTAGE
Select playback failure percentage in the statistics presence mask.

\def ALUMBRA_STATISTIC_AVERAGE_PLAYBACK_STALLED_COUNT
Select average playback stalled count in the statistics presence mask.

\def ALUMBRA_STATISTIC_AVERAGE_STALLED_TIME_PERCENTAGE
Select average stalled time percentage in the statistics presence mask.

\def ALUMBRA_STATISTIC_AVERAGE_PLAYBACK_BITRATE
Select average playback bitrate in the statistics presence mask.

\def ALUMBRA_STATISTIC_AVERAGE_SEGMENT_BITRATE
Select average segment bitrate in the statistics presence mask.

\def ALUMBRA_STATISTIC_RENDITION_SWITCH_COUNT
Select rendition switch count in the statistics presence mask.

\def ALUMBRA_STATISTIC_DROPPED_FRAMES
Select dropped frames in the statistics presence mask.

\def ALUMBRA_STATISTIC_LIVE_LATENCY
Select live latency in the statistics presence mask.

\fn uint32_t alumbra_c_api_version(void)
Returns the ABI version implemented by the library.

\fn alumbra_status alumbra_negotiate_version(alumbra_version_info* info)
Checks the requested ABI version and reports the supported version.

\fn alumbra_status alumbra_player_create(const alumbra_player_config* config, alumbra_handle* player)
Creates a player and writes its handle.

\fn alumbra_status alumbra_player_destroy(alumbra_handle player)
Destroys the player and invalidates its handle.

\fn alumbra_status alumbra_player_get_source(alumbra_handle player, char* source, size_t* source_size)
Reads the source URL into a caller-supplied buffer.

\fn alumbra_status alumbra_player_set_source(alumbra_handle player, const char* source, size_t source_size)
Sets the source URL from a pointer and byte count.

\fn alumbra_status alumbra_player_load(alumbra_handle player)
Runs the player's load algorithm.

\fn alumbra_status alumbra_player_play(alumbra_handle player)
Requests playback.

\fn alumbra_status alumbra_player_pause(alumbra_handle player)
Pauses playback.

\fn alumbra_status alumbra_player_fast_seek(alumbra_handle player, double seconds)
Requests a fast seek to the specified time in seconds.

\fn alumbra_status alumbra_player_can_play_type(alumbra_handle player, const char* type, size_t type_size, uint32_t* result)
Returns the support level for a media type.

\fn alumbra_status alumbra_player_get_paused(alumbra_handle player, uint8_t* paused)
Reads whether playback is paused.

\fn alumbra_status alumbra_player_get_ended(alumbra_handle player, uint8_t* ended)
Reads whether playback reached the end.

\fn alumbra_status alumbra_player_get_autoplay(alumbra_handle player, uint8_t* autoplay)
Reads the autoplay flag.

\fn alumbra_status alumbra_player_set_autoplay(alumbra_handle player, uint8_t autoplay)
Sets the autoplay flag.

\fn alumbra_status alumbra_player_get_loop(alumbra_handle player, uint8_t* loop)
Reads the loop flag.

\fn alumbra_status alumbra_player_set_loop(alumbra_handle player, uint8_t loop)
Sets the loop flag.

\fn alumbra_status alumbra_player_get_duration(alumbra_handle player, double* seconds)
Reads the duration in seconds.

\fn alumbra_status alumbra_player_get_preload(alumbra_handle player, uint32_t* preload)
Reads the preload mode.

\fn alumbra_status alumbra_player_set_preload(alumbra_handle player, uint32_t preload)
Sets the preload mode.

\fn alumbra_status alumbra_player_get_default_playback_rate(alumbra_handle player, double* rate)
Reads the default playback rate.

\fn alumbra_status alumbra_player_set_default_playback_rate(alumbra_handle player, double rate)
Sets the default playback rate.

\fn alumbra_status alumbra_player_get_preserves_pitch(alumbra_handle player, uint8_t* preserves_pitch)
Reads whether pitch is preserved during rate changes.

\fn alumbra_status alumbra_player_set_preserves_pitch(alumbra_handle player, uint8_t preserves_pitch)
Sets whether pitch is preserved during rate changes.

\fn alumbra_status alumbra_player_get_time_ranges(alumbra_handle player, uint32_t kind, size_t* count)
Returns the number of buffered or seekable time ranges.

\fn alumbra_status alumbra_player_get_time_range(alumbra_handle player, uint32_t kind, size_t index, double* start, double* end)
Reads a buffered or seekable time range in seconds.

\fn alumbra_status alumbra_player_get_current_time(alumbra_handle player, double* seconds)
Reads the current playback time in seconds.

\fn alumbra_status alumbra_player_set_current_time(alumbra_handle player, double seconds)
Requests a seek to the specified time in seconds.

\fn alumbra_status alumbra_player_get_seeking(alumbra_handle player, uint8_t* seeking)
Reads whether the player is seeking as zero or one.

\fn alumbra_status alumbra_player_get_volume(alumbra_handle player, double* volume)
Reads the volume in the range zero to one.

\fn alumbra_status alumbra_player_set_volume(alumbra_handle player, double volume)
Sets the volume in the range zero to one.

\fn alumbra_status alumbra_player_get_muted(alumbra_handle player, uint8_t* muted)
Reads the muted state as zero or one.

\fn alumbra_status alumbra_player_set_muted(alumbra_handle player, uint8_t muted)
Sets the muted state from zero or one.

\fn alumbra_status alumbra_player_get_playback_rate(alumbra_handle player, double* rate)
Reads the playback rate.

\fn alumbra_status alumbra_player_set_playback_rate(alumbra_handle player, double rate)
Sets a finite, non-negative playback rate.

\fn alumbra_status alumbra_player_get_ready_state(alumbra_handle player, uint32_t* state)
Reads the HTML ready state value from zero to four.

\fn alumbra_status alumbra_player_get_network_state(alumbra_handle player, uint32_t* state)
Reads the HTML network state value from zero to three.

\fn alumbra_status alumbra_player_get_statistics(alumbra_handle player, alumbra_statistics* statistics)
Reads the statistics snapshot and identifies populated values in its presence mask.

\fn alumbra_status alumbra_player_get_error(alumbra_handle player, alumbra_error_info* error, char* message, size_t* message_size)
Reads the current media error and copies its message when a buffer is supplied.

\fn alumbra_status alumbra_player_poll_event(alumbra_handle player, alumbra_event* event, uint8_t* available)
Dispatches and pulls the next event, setting `available` to zero when the queue is empty.

\fn alumbra_status alumbra_settings_get(alumbra_handle player, const char* key, size_t key_size, char* value, size_t* value_size)
Reads an effective setting into a caller-supplied buffer.

\fn alumbra_status alumbra_settings_set(alumbra_handle player, const char* key, size_t key_size, const char* value, size_t value_size)
Sets a user-layer setting from a pointer and byte count.

\fn alumbra_status alumbra_player_text_track_count(alumbra_handle player, size_t* count)
Returns the number of text tracks on a player.

\fn alumbra_status alumbra_player_text_track_at(alumbra_handle player, size_t index, alumbra_track_handle* track)
Acquires a generation-checked handle for the text track at an index.

\fn alumbra_status alumbra_player_add_text_track(alumbra_handle player, uint32_t kind, const char* label, size_t label_size, const char* language, size_t language_size, alumbra_track_handle* track)
Adds a text track and returns its handle.

\fn alumbra_status alumbra_text_track_release(alumbra_track_handle track)
Releases a text-track handle.

\fn alumbra_status alumbra_text_track_get_kind(alumbra_track_handle track, uint32_t* kind)
Reads the text-track kind.

\fn alumbra_status alumbra_text_track_get_mode(alumbra_track_handle track, uint32_t* mode)
Reads the text-track mode.

\fn alumbra_status alumbra_text_track_set_mode(alumbra_track_handle track, uint32_t mode)
Sets the text-track mode.

\fn alumbra_status alumbra_text_track_get_id(alumbra_track_handle track, char* id, size_t* id_size)
Copies the text-track identifier to a caller-supplied buffer.

\fn alumbra_status alumbra_text_track_get_label(alumbra_track_handle track, char* label, size_t* label_size)
Copies the text-track label to a caller-supplied buffer.

\fn alumbra_status alumbra_text_track_get_language(alumbra_track_handle track, char* language, size_t* language_size)
Copies the text-track language to a caller-supplied buffer.
