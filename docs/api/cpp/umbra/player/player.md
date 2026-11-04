\file umbra/player/player.h
The `HTMLMediaElement`-shaped player of [ADR-0023](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0023-public-api-surface-and-media-error.md).

Every member follows the WHATWG HTML algorithms for the media element: resource selection, the load algorithm,
`readyState` transitions, seeking and the list of pending play promises. A member that is not built yet is absent,
never stubbed. The
[conformance table](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/reference/html-media-element-conformance.md)
lists every HTML member and event with its C++ name, stability label and covering tests.

C++ names follow one mechanical rule: `Play()` is `play()`, `current_time()` is `currentTime`, and
`set_current_time()` is its setter. Times are seconds as `double`, as in HTML. Setters that HTML would answer with
an exception return `false` instead, because the public API does not throw.

A player is thread-compatible. Create it on the sequence that owns it, call every method on that sequence, and
destroy it there. Attributes that HTML updates synchronously, such as `paused` after `Pause()` and `current_time()`
after a seek, change at once. Play callbacks and event listeners run from DispatchEvents(), on that same
sequence.

\include use_player.cc

\namespace umbra::player
The public player API: the media element subset, its state enumerations, play results, media errors and time
ranges.

\struct umbra::player::PlayerOptions
Embedder hooks and configuration for Player::Create().

\enum umbra::player::FastSeekSnap
The keyframe selection policy used by Player::FastSeek(). `kPrevious` is the default and selects the previous
keyframe. `kNearest` selects the closest keyframe.

\var umbra::player::FastSeekSnap::kPrevious
Selects the keyframe immediately before the requested position.

\var umbra::player::FastSeekSnap::kNearest
Selects the keyframe closest to the requested position.

\var umbra::player::PlayerOptions::post_task
Posts a task to the sequence that owns the player, to run after the delay. Required.

The sequence must run tasks in order and support delayed tasks. The player posts a task for every port callback,
and the player itself must only be used on that sequence.

\var umbra::player::PlayerOptions::runs_on_sequence
Optional check that the caller is on the player's sequence, used by debug assertions. When empty, every caller is
assumed to be on the sequence.

\var umbra::player::PlayerOptions::on_events_available
Optional hook called when the event queue becomes non-empty ("notify, then pull", ADR-0015).

The hook must be lightweight and must not call into the player. Typically it posts a task that calls
Player::DispatchEvents(). It is never called while an internal lock is held.

\var umbra::player::PlayerOptions::allow_playback
Optional platform policy. When it returns false, Player::Play() completes with PlayResultCode::kNotAllowed and
autoplay does not start playback, as a browser's autoplay policy does.

\var umbra::player::PlayerOptions::qoe_enabled
Optional reader for this player's `qoe.enabled` setting. Bind it to this player's settings store; when empty,
QoE collection is enabled. A false value clears and disables only this player's statistics model. Keep the store
alive until the Player is destroyed, or capture shared ownership in the reader.

\var umbra::player::PlayerOptions::log_sink
Optional per-player sink for structured log records. Runtime severity and category filtering comes from this player's
`observability.log.*` settings. The default is an empty sink.

\var umbra::player::PlayerOptions::trace_sink
Optional per-player sink for trace records. Enabled categories come from this player's
`observability.trace.categories` setting. The default is an empty sink.

\var umbra::player::PlayerOptions::headless
Creates the build's default port without a visible window or audio device where supported. Defaults to false.

\var umbra::player::PlayerOptions::preroll_ms
Media duration required ahead of the play position before playback starts. It must be from 0 to 10000 milliseconds.

\var umbra::player::PlayerOptions::fast_seek_snap
Keyframe selection policy used by FastSeek().

\var umbra::player::PlayerOptions::text_cue_memory_budget_kb
Maximum cue storage for each text track, in kibibytes. It must be from 64 to 65536.

\var umbra::player::PlayerOptions::minimum_audible_rate
Lowest playback rate at which converted audio is audible. Defaults to 0.25 and must be between 0.25 and 4.0,
inclusive. This is the `playback.rate.min` setting.

\var umbra::player::PlayerOptions::maximum_audible_rate
Highest playback rate at which converted audio is audible. Defaults to 4.0 and must be between the minimum
audible rate and 4.0, inclusive. This is the `playback.rate.max` setting.

\class umbra::player::Player
A media element with the HTML `HTMLMediaElement` semantics, without the DOM.

Create it with Player::Create(). The destructor stops the port, drops the queued events and destroys the callbacks
of pending Player::Play() calls without running them.

\fn static std::unique_ptr<Player> umbra::player::Player::Create(PlayerOptions options)
Creates a player that runs on the sequence of `options.post_task`.

Returns null when `post_task` is empty, `preroll_ms` is outside 0 to 10000 or `timeupdate_interval_ms` is
outside 15 to 250. The build's default port is created when available. Until the progressive sources are connected,
setting `src` makes the load fail with `MEDIA_ERR_SRC_NOT_SUPPORTED`, which the example shows. HTML counterpart: the
`HTMLMediaElement` constructors, which have no equivalent.

\fn Settings& umbra::player::Player::settings()
Returns this player's typed settings layer. Values are validated against the generated registry, and a rejected
write returns its structured ErrorDetail while preserving the previous value.

\fn const Settings& umbra::player::Player::settings() const
Returns this player's typed settings layer for reading.

\fn umbra::player::Player::~Player()
Shuts the player down in the order of ADR-0015 and releases it.

Destroying the player from one of its own callbacks is allowed; the remaining events are dropped.

\fn const std::string& umbra::player::Player::src() const
Returns the current source URL, or an empty string before the first set_src().

HTML counterpart: `src`, the reflecting content attribute. Alumbra has no attribute, so a source URL that was set
and an empty string that was set are told apart only by their effect on the load.

\fn void umbra::player::Player::set_src(std::string src)
Sets the source URL and runs the media element load algorithm.

HTML counterpart: setting `src`. An empty string selects no usable resource, so the load fails with
`MEDIA_ERR_SRC_NOT_SUPPORTED`, as an empty `src` attribute does. Deviation: URLs are opaque strings resolved by the
data sources, and a URL no source handles also fails with `MEDIA_ERR_SRC_NOT_SUPPORTED`.

\fn void umbra::player::Player::Load()
Runs the media element load algorithm again with the current source.

HTML counterpart: `load()`. It aborts pending play calls with PlayResultCode::kAbort, resets the element and clears
the error. Without a source it only resets the element.

\fn CanPlayTypeResult umbra::player::Player::CanPlayType(std::string_view type) const
Reports how likely the player is to play a MIME type, with an optional `codecs` parameter.

HTML counterpart: `canPlayType()`. The container must be one the engine declares (MP4, WebM, FLAC or MPEG audio)
and every codec in `codecs` must parse and fit the container. With a capability provider, each codec is also
queried and the result is CanPlayTypeResult::kProbably when all are supported. Without one the result never
exceeds CanPlayTypeResult::kMaybe. Any other parameter gives CanPlayTypeResult::kNone.

\fn NetworkState umbra::player::Player::network_state() const
Returns the current network state.

HTML counterpart: `networkState`, with the same four values and transitions.

\fn void umbra::player::Player::Play(PlayCallback done)
Starts or resumes playback and reports the outcome through `done`.

HTML counterpart: `play()`. The callback replaces the returned promise and runs exactly once, from
DispatchEvents(), ordered against the `play`, `playing` and `pause` events as HTML orders the promise. Results:
PlayResultCode::kOk when playback has started, PlayResultCode::kAbort when `Pause()` or `Load()` interrupted it,
PlayResultCode::kNotAllowed when platform policy refuses and PlayResultCode::kNotSupported, with the detail of the
media error, after a fatal error before metadata. If the player is destroyed first, the callback is destroyed
without running. Calling `Play()` at the end of the media seeks to the start.

\fn void umbra::player::Player::Pause()
Pauses playback.

HTML counterpart: `pause()`. Pending Player::Play() calls complete with PlayResultCode::kAbort.

\fn bool umbra::player::Player::paused() const
Returns whether playback is paused.

HTML counterpart: `paused`. It changes at once when Player::Play() or Player::Pause() is called.

\fn bool umbra::player::Player::ended() const
Returns whether playback has reached the end of the media.

HTML counterpart: `ended`.

\fn bool umbra::player::Player::autoplay() const
Returns whether playback starts as soon as enough data is buffered.

HTML counterpart: `autoplay`.

\fn void umbra::player::Player::set_autoplay(bool autoplay)
Sets whether playback starts as soon as enough data is buffered.

HTML counterpart: setting `autoplay`. Autoplay is subject to PlayerOptions::allow_playback.

\fn bool umbra::player::Player::loop() const
Returns whether playback restarts from the start at the end of the media.

HTML counterpart: `loop`.

\fn void umbra::player::Player::set_loop(bool loop)
Sets whether playback restarts from the start at the end of the media.

HTML counterpart: setting `loop`.

\fn double umbra::player::Player::current_time() const
Returns the current playback position in seconds.

HTML counterpart: `currentTime`. Before metadata it is the default playback start position, which is 0 unless
set_current_time() ran before the metadata arrived.

\fn bool umbra::player::Player::set_current_time(double seconds)
Seeks to a position in seconds.

HTML counterpart: setting `currentTime`. Before metadata the value becomes the default playback start position and
playback seeks to it when the metadata arrives. Returns false, and does nothing, for NaN and infinite values, where
HTML throws a `TypeError`. The position is clamped to the media, and `seeking` and `current_time()` change at once.

\fn bool umbra::player::Player::FastSeek(double seconds)
Seeks to a position in seconds, favouring speed over accuracy.

HTML counterpart: `fastSeek()`. HTML allows the exact position, and there is no keyframe index yet, so this seeks
to a previous keyframe by default, using `playback.seek.fast_mode_snap`. The `nearest` policy selects the nearest
keyframe. Before metadata, the request becomes the default start position. Returns false for NaN and infinite values.

\fn bool umbra::player::Player::seeking() const
Returns whether a seek is in progress.

HTML counterpart: `seeking`.

\fn double umbra::player::Player::duration() const
Returns the length of the media in seconds.

HTML counterpart: `duration`. It is NaN before the metadata is known and +Infinity for a stream with no announced
end.

\fn TimeRanges umbra::player::Player::buffered() const
Returns the range of media that is buffered.

HTML counterpart: `buffered`. It is the intersection of the selected tracks' ranges, so it holds at most one range
that starts where the last seek or load started. Deviation: HTML allows several disjoint ranges, which the player
will report once seeks use buffered data without a flush (#91).

\fn TimeRanges umbra::player::Player::seekable() const
Returns the range of media that can be seeked to.

HTML counterpart: `seekable`. It is empty before metadata and for a stream whose duration is unbounded, and
otherwise covers 0 to `duration`. DVR windows of live streams follow with the live model (#91).

\fn ReadyState umbra::player::Player::ready_state() const
Returns how much data the element has.

HTML counterpart: `readyState`, computed as the lowest level among the selected tracks as ADR-0023 decides.

\fn Preload umbra::player::Player::preload() const
Returns the preload hint.

HTML counterpart: `preload`. The default is Preload::kMetadata.

\fn void umbra::player::Player::set_preload(Preload preload)
Sets the preload hint.

HTML counterpart: setting `preload`. HTML defines it as a hint. With `none` the player does not fetch until
`Play()` or until the hint changes, and fires `suspend` after `loadstart`. `metadata` and `auto` behave alike,
because the player always prefetches enough data to reach `HAVE_ENOUGH_DATA`.

\fn double umbra::player::Player::playback_rate() const
Returns the current playback rate.

HTML counterpart: `playbackRate`.

\fn bool umbra::player::Player::set_playback_rate(double rate)
Sets the current playback rate.

HTML counterpart: setting `playbackRate`. Deviation, from ADR-0024: only finite, non-negative rates are accepted,
because reverse playback is out of scope. Other values return false and change nothing. A change fires `ratechange`.

\fn double umbra::player::Player::default_playback_rate() const
Returns the rate that the next load starts with.

HTML counterpart: `defaultPlaybackRate`.

\fn bool umbra::player::Player::set_default_playback_rate(double rate)
Sets the rate that the next load starts with.

HTML counterpart: setting `defaultPlaybackRate`, with the restriction of set_playback_rate().

\fn bool umbra::player::Player::preserves_pitch() const
Returns whether the audio pitch is preserved when the rate is not 1.

HTML counterpart: `preservesPitch`. The default is true.

\fn void umbra::player::Player::set_preserves_pitch(bool preserves_pitch)
Sets whether the audio pitch is preserved when the rate is not 1.

HTML counterpart: setting `preservesPitch`. The value is sent to the active port and applied to converted audio.

\fn double umbra::player::Player::volume() const
Returns the audio volume from 0 to 1.

HTML counterpart: `volume`.

\fn bool umbra::player::Player::set_volume(double volume)
Sets the audio volume.

HTML counterpart: setting `volume`. Returns false, and changes nothing, for values outside 0 to 1 and for NaN,
where HTML throws `IndexSizeError`. A change fires `volumechange`.

\fn bool umbra::player::Player::muted() const
Returns whether the audio is muted.

HTML counterpart: `muted`.

\fn void umbra::player::Player::set_muted(bool muted)
Sets whether the audio is muted.

HTML counterpart: setting `muted`. Muting keeps `volume`.

\fn std::optional<MediaError> umbra::player::Player::error() const
Returns the fatal error, or an empty value when there is none.

HTML counterpart: `error`, which is null when empty. Load() clears it.

\fn StatisticsSnapshot umbra::player::Player::Statistics(bool include_unstable_debug) const
Returns this player's event-derived statistics snapshot. Collection follows this player's `qoe.enabled` setting;
disabling it clears this player's snapshot and prevents subsequent events from being timestamped or aggregated.
The unstable debug extension is absent unless explicitly requested.

\fn TextTrackList& umbra::player::Player::text_tracks()
Returns the ordered text-track collection.

HTML counterpart: `textTracks`. Its list events run synchronously on the player sequence.

\fn std::shared_ptr<TextTrack> umbra::player::Player::AddTextTrack(TextTrackKind kind, std::string label, std::string language)
Creates and adds an app-generated text track using the configured cue-memory budget.

HTML counterpart: `addTextTrack()`.

\fn void umbra::player::Player::DispatchEvents()
Delivers the queued events to their listeners and the queued play results to their callbacks, in the order they
were queued.

Alumbra extension, the "pull" of ADR-0015's notify-then-pull delivery. Call it from the task that
PlayerOptions::on_events_available posts. Listeners and callbacks may call any method, including destroying the
player; entries that are still queued then are dropped, so no event is delivered after the player is destroyed.

\fn template <typename Event> Subscription umbra::player::Player::AddEventListener(std::function<void(const Event&)> listener)
Registers a listener for one event type and returns the handle that keeps it registered.

Alumbra extension, the typed form of `addEventListener()`. `Event` is one of the event structs of events.h, such as
LoadStartEvent. Listeners of an event run in registration order, from DispatchEvents(), after the state change
that caused the event. A listener added while an event is delivered sees only later events, and one that is
cancelled during delivery is not called.
