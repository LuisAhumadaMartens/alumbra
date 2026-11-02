\file umbra/c_api/c_api.h

# C ABI

**Stability: experimental.** The C ABI may change in any release before 1.0.

The installed `umbra/c_api/c_api.h` header is valid C11 and C++. Its exported names
begin with `alumbra_`. `ALUMBRA_C_API_VERSION` identifies the ABI revision;
call `alumbra_negotiate_version` before using another entry point.

Every extensible structure begins with `struct_size`. Set it to the size of the
structure supplied by the caller. A callee checks the fields it uses and must
not read beyond the supplied size. New fields are appended. A caller should
zero-initialise structures before setting fields.

Handles are unsigned identifiers. They do not expose addresses and are owned by
the player that created them. Destroying a player invalidates its handle.
Operations on stale handles return `ALUMBRA_STATUS_INVALID_HANDLE`.
The lower 32 bits identify a registry slot and the upper 32 bits hold its
generation. Releasing a slot advances its generation before reuse; zero is
reserved as an invalid handle.

Strings are pointer and byte-count pairs. The byte count excludes any trailing
zero byte, and input strings need not be zero terminated. The caller retains
string storage for the duration of the call. Output buffers report their
required size through the associated size pointer.

The notify hook carries no payload and may run on any engine thread. It should
only schedule a drain on the embedder's chosen sequence. Event payloads are
obtained by polling and must be consumed on that sequence. Player calls do not
transfer ownership of callback data.

The first implementation stage exposes the version negotiation and the full
surface shape. Player, event and settings operations currently return
`ALUMBRA_STATUS_UNSUPPORTED` until the desktop port and settings runtime are
connected to the ABI.

\def ALUMBRA_C_API_VERSION
The current C ABI revision.

\typedef alumbra_handle
An opaque, generation-checked player identifier.

\typedef alumbra_status
Result codes returned by ABI calls.

\struct alumbra_version_info
Version negotiation input and output. `requested_version` is supplied by the
caller and `supported_version` is written by the library.

\typedef alumbra_version_info
The version negotiation structure.

\struct alumbra_player_config
Player creation options. `notify` is called when queued events become
available; `user_data` is passed to that hook.

\typedef alumbra_player_config
The player configuration structure.

\struct alumbra_event
One event pulled from a player queue. `type` identifies the event and
`sequence` preserves queue order.

\typedef alumbra_event
The event structure returned by polling.

\var alumbra_version_info::struct_size
The size in bytes of the structure supplied by the caller.

\var alumbra_version_info::requested_version
The ABI revision requested by the caller.

\var alumbra_version_info::supported_version
The ABI revision implemented by the library.

\var alumbra_player_config::struct_size
The size in bytes of the configuration structure.

\var alumbra_player_config::notify
The payload-free notification hook.

\var alumbra_player_config::user_data
The embedder context passed to the notification hook.

\var alumbra_event::struct_size
The size in bytes of the event structure.

\var alumbra_event::type
The event type identifier.

\var alumbra_event::sequence
The event's ordering sequence number.

\var alumbra_event::current_time
The current playback time in seconds.

\var alumbra_event::duration
The media duration in seconds.

\def ALUMBRA_STATUS_OK
The call succeeded.

\def ALUMBRA_STATUS_INVALID_ARGUMENT
The caller supplied an invalid argument or structure size.

\def ALUMBRA_STATUS_INVALID_HANDLE
The handle is zero, stale or does not identify a player.

\def ALUMBRA_STATUS_UNSUPPORTED
The requested operation is not available in this implementation.

\def ALUMBRA_STATUS_BUFFER_TOO_SMALL
The output buffer does not have the required capacity.

\fn uint32_t alumbra_c_api_version(void)
Returns the ABI version implemented by the library.

\fn alumbra_status alumbra_negotiate_version(alumbra_version_info* info)
Checks the requested ABI version and reports the supported version.

\fn alumbra_status alumbra_player_create(const alumbra_player_config* config, alumbra_handle* player)
Creates a player and writes its handle.

\fn alumbra_status alumbra_player_destroy(alumbra_handle player)
Destroys the player and invalidates its handle.

\fn alumbra_status alumbra_player_set_source(alumbra_handle player, const char* source, size_t source_size)
Sets the source URL from a pointer and byte count.

\fn alumbra_status alumbra_player_load(alumbra_handle player)
Runs the player's load algorithm.

\fn alumbra_status alumbra_player_play(alumbra_handle player)
Requests playback.

\fn alumbra_status alumbra_player_pause(alumbra_handle player)
Pauses playback.

\fn alumbra_status alumbra_player_poll_event(alumbra_handle player, alumbra_event* event, uint8_t* available)
Pulls the next event, setting `available` to zero when the queue is empty.

\fn alumbra_status alumbra_settings_get(alumbra_handle player, const char* key, size_t key_size, char* value, size_t* value_size)
Reads a setting value into a caller supplied buffer.

\fn alumbra_status alumbra_settings_set(alumbra_handle player, const char* key, size_t key_size, const char* value, size_t value_size)
Sets a setting value from a pointer and byte count.
