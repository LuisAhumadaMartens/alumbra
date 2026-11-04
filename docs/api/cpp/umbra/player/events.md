\file umbra/player/events.h
The events of the media element and the subscription handle that listeners return.

Each event has its own C++ type, and every event of a player goes through the one queue of ADR-0015, so their
relative order is the order of the HTML algorithms. Every event carries the attribute values at the moment it was
queued in ElementState, not the values at the moment a listener runs. Listeners are registered with
Player::AddEventListener() and run from Player::DispatchEvents().

\include observe_events.cc

\enum umbra::player::EventType
Identifies an event type, for the listener registry of the player.

\var umbra::player::EventType::kLoadStart
The `loadstart` event: LoadStartEvent.

\var umbra::player::EventType::kProgress
The `progress` event: ProgressEvent.

\var umbra::player::EventType::kSuspend
The `suspend` event: SuspendEvent.

\var umbra::player::EventType::kAbort
The `abort` event: AbortEvent.

\var umbra::player::EventType::kError
The `error` event: ErrorEvent.

\var umbra::player::EventType::kEmptied
The `emptied` event: EmptiedEvent.

\var umbra::player::EventType::kStalled
The `stalled` event: StalledEvent.

\var umbra::player::EventType::kLoadedMetadata
The `loadedmetadata` event: LoadedMetadataEvent.

\var umbra::player::EventType::kLoadedData
The `loadeddata` event: LoadedDataEvent.

\var umbra::player::EventType::kCanPlay
The `canplay` event: CanPlayEvent.

\var umbra::player::EventType::kCanPlayThrough
The `canplaythrough` event: CanPlayThroughEvent.

\var umbra::player::EventType::kPlaying
The `playing` event: PlayingEvent.

\var umbra::player::EventType::kWaiting
The `waiting` event: WaitingEvent.

\var umbra::player::EventType::kSeeking
The `seeking` event: SeekingEvent.

\var umbra::player::EventType::kSeeked
The `seeked` event: SeekedEvent.

\var umbra::player::EventType::kEnded
The `ended` event: EndedEvent.

\var umbra::player::EventType::kDurationChange
The `durationchange` event: DurationChangeEvent.

\var umbra::player::EventType::kTimeUpdate
The `timeupdate` event: TimeUpdateEvent.

\var umbra::player::EventType::kPlay
The `play` event: PlayEvent.

\var umbra::player::EventType::kPause
The `pause` event: PauseEvent.

\var umbra::player::EventType::kRateChange
The `ratechange` event: RateChangeEvent.

\var umbra::player::EventType::kResize
The `resize` event: ResizeEvent.

\var umbra::player::EventType::kVolumeChange
The `volumechange` event: VolumeChangeEvent.

\var umbra::player::EventType::kWarning
The `warning` event: WarningEvent.

\var umbra::player::EventType::kAudioTrackAdd
The audio list's `addtrack` event.

\var umbra::player::EventType::kAudioTrackRemove
The audio list's `removetrack` event.

\var umbra::player::EventType::kAudioTracksChange
The audio list's `change` event.

\var umbra::player::EventType::kVideoTrackAdd
The video list's `addtrack` event.

\var umbra::player::EventType::kVideoTrackRemove
The video list's `removetrack` event.

\var umbra::player::EventType::kVideoTracksChange
The video list's `change` event.

\var umbra::player::EventType::kTextTrackAdd
The text list's `addtrack` event.

\var umbra::player::EventType::kTextTrackRemove
The text list's `removetrack` event.

\var umbra::player::EventType::kTextTracksChange
The text list's `change` event.

\struct umbra::player::ElementState
The attribute values of the element at the moment an event was queued.

Alumbra extension. HTML events carry no payload, and listeners read the attributes of the element instead, which
may have moved on by the time the listener runs. `duration` is NaN before the metadata is known and +Infinity for a
stream with no announced end.

\var umbra::player::ElementState::network_state
The value of `networkState`.

\var umbra::player::ElementState::ready_state
The value of `readyState`.

\var umbra::player::ElementState::paused
The value of `paused`.

\var umbra::player::ElementState::seeking
The value of `seeking`.

\var umbra::player::ElementState::ended
The value of `ended`.

\var umbra::player::ElementState::current_time
The value of `currentTime` in seconds.

\var umbra::player::ElementState::duration
The value of `duration` in seconds.

\struct umbra::player::LoadStartEvent
The `loadstart` event: The load algorithm started resource selection.

\var umbra::player::LoadStartEvent::kType
The event type that selects this event in Player::AddEventListener().

\var umbra::player::LoadStartEvent::kName
The event name, `loadstart`, which the bindings use.

\var umbra::player::LoadStartEvent::state
The attribute values when the event was queued.

\struct umbra::player::ProgressEvent
The `progress` event: The player is fetching media data. It fires about every 350 ms while data arrives, and once more when the fetch completes.

\var umbra::player::ProgressEvent::kType
The event type that selects this event in Player::AddEventListener().

\var umbra::player::ProgressEvent::kName
The event name, `progress`, which the bindings use.

\var umbra::player::ProgressEvent::state
The attribute values when the event was queued.

\struct umbra::player::SuspendEvent
The `suspend` event: The player stopped fetching, because the fetch completed or `preload` is `none`.

\var umbra::player::SuspendEvent::kType
The event type that selects this event in Player::AddEventListener().

\var umbra::player::SuspendEvent::kName
The event name, `suspend`, which the bindings use.

\var umbra::player::SuspendEvent::state
The attribute values when the event was queued.

\struct umbra::player::AbortEvent
The `abort` event: A load was aborted before it completed and without an error.

\var umbra::player::AbortEvent::kType
The event type that selects this event in Player::AddEventListener().

\var umbra::player::AbortEvent::kName
The event name, `abort`, which the bindings use.

\var umbra::player::AbortEvent::state
The attribute values when the event was queued.

\struct umbra::player::ErrorEvent
The `error` event: A fatal error occurred. ErrorEvent::error carries the MediaError.

\var umbra::player::ErrorEvent::kType
The event type that selects this event in Player::AddEventListener().

\var umbra::player::ErrorEvent::kName
The event name, `error`, which the bindings use.

\var umbra::player::ErrorEvent::state
The attribute values when the event was queued.

\var umbra::player::ErrorEvent::error
The fatal error, with its HTML code, message and ErrorDetail.

\struct umbra::player::EmptiedEvent
The `emptied` event: The element was reset by a load after a resource had been selected.

\var umbra::player::EmptiedEvent::kType
The event type that selects this event in Player::AddEventListener().

\var umbra::player::EmptiedEvent::kName
The event name, `emptied`, which the bindings use.

\var umbra::player::EmptiedEvent::state
The attribute values when the event was queued.

\struct umbra::player::StalledEvent
The `stalled` event: The player tried to fetch data and none arrived for about three seconds.

\var umbra::player::StalledEvent::kType
The event type that selects this event in Player::AddEventListener().

\var umbra::player::StalledEvent::kName
The event name, `stalled`, which the bindings use.

\var umbra::player::StalledEvent::state
The attribute values when the event was queued.

\struct umbra::player::LoadedMetadataEvent
The `loadedmetadata` event: The duration and tracks are known.

\var umbra::player::LoadedMetadataEvent::kType
The event type that selects this event in Player::AddEventListener().

\var umbra::player::LoadedMetadataEvent::kName
The event name, `loadedmetadata`, which the bindings use.

\var umbra::player::LoadedMetadataEvent::state
The attribute values when the event was queued.

\struct umbra::player::LoadedDataEvent
The `loadeddata` event: Data for the current position is available for the first time since the load.

\var umbra::player::LoadedDataEvent::kType
The event type that selects this event in Player::AddEventListener().

\var umbra::player::LoadedDataEvent::kName
The event name, `loadeddata`, which the bindings use.

\var umbra::player::LoadedDataEvent::state
The attribute values when the event was queued.

\struct umbra::player::CanPlayEvent
The `canplay` event: `readyState` reached `HAVE_FUTURE_DATA` from a lower level.

\var umbra::player::CanPlayEvent::kType
The event type that selects this event in Player::AddEventListener().

\var umbra::player::CanPlayEvent::kName
The event name, `canplay`, which the bindings use.

\var umbra::player::CanPlayEvent::state
The attribute values when the event was queued.

\struct umbra::player::CanPlayThroughEvent
The `canplaythrough` event: `readyState` reached `HAVE_ENOUGH_DATA`.

\var umbra::player::CanPlayThroughEvent::kType
The event type that selects this event in Player::AddEventListener().

\var umbra::player::CanPlayThroughEvent::kName
The event name, `canplaythrough`, which the bindings use.

\var umbra::player::CanPlayThroughEvent::state
The attribute values when the event was queued.

\struct umbra::player::PlayingEvent
The `playing` event: Playback started or resumed after pausing or waiting for data.

\var umbra::player::PlayingEvent::kType
The event type that selects this event in Player::AddEventListener().

\var umbra::player::PlayingEvent::kName
The event name, `playing`, which the bindings use.

\var umbra::player::PlayingEvent::state
The attribute values when the event was queued.

\struct umbra::player::WaitingEvent
The `waiting` event: Playback stopped because the next frame is not available yet.

\var umbra::player::WaitingEvent::kType
The event type that selects this event in Player::AddEventListener().

\var umbra::player::WaitingEvent::kName
The event name, `waiting`, which the bindings use.

\var umbra::player::WaitingEvent::state
The attribute values when the event was queued.

\struct umbra::player::SeekingEvent
The `seeking` event: A seek started.

\var umbra::player::SeekingEvent::kType
The event type that selects this event in Player::AddEventListener().

\var umbra::player::SeekingEvent::kName
The event name, `seeking`, which the bindings use.

\var umbra::player::SeekingEvent::state
The attribute values when the event was queued.

\struct umbra::player::SeekedEvent
The `seeked` event: A seek finished.

\var umbra::player::SeekedEvent::kType
The event type that selects this event in Player::AddEventListener().

\var umbra::player::SeekedEvent::kName
The event name, `seeked`, which the bindings use.

\var umbra::player::SeekedEvent::state
The attribute values when the event was queued.

\struct umbra::player::EndedEvent
The `ended` event: Playback reached the end of the media.

\var umbra::player::EndedEvent::kType
The event type that selects this event in Player::AddEventListener().

\var umbra::player::EndedEvent::kName
The event name, `ended`, which the bindings use.

\var umbra::player::EndedEvent::state
The attribute values when the event was queued.

\struct umbra::player::DurationChangeEvent
The `durationchange` event: The duration changed.

\var umbra::player::DurationChangeEvent::kType
The event type that selects this event in Player::AddEventListener().

\var umbra::player::DurationChangeEvent::kName
The event name, `durationchange`, which the bindings use.

\var umbra::player::DurationChangeEvent::state
The attribute values when the event was queued.

\struct umbra::player::TimeUpdateEvent
The `timeupdate` event: The current position changed. During playback it fires at the configured interval, from 15 to 250 ms.

\var umbra::player::TimeUpdateEvent::kType
The event type that selects this event in Player::AddEventListener().

\var umbra::player::TimeUpdateEvent::kName
The event name, `timeupdate`, which the bindings use.

\var umbra::player::TimeUpdateEvent::state
The attribute values when the event was queued.

\struct umbra::player::PlayEvent
The `play` event: `paused` became false.

\var umbra::player::PlayEvent::kType
The event type that selects this event in Player::AddEventListener().

\var umbra::player::PlayEvent::kName
The event name, `play`, which the bindings use.

\var umbra::player::PlayEvent::state
The attribute values when the event was queued.

\struct umbra::player::PauseEvent
The `pause` event: `paused` became true.

\var umbra::player::PauseEvent::kType
The event type that selects this event in Player::AddEventListener().

\var umbra::player::PauseEvent::kName
The event name, `pause`, which the bindings use.

\var umbra::player::PauseEvent::state
The attribute values when the event was queued.

\struct umbra::player::RateChangeEvent
The `ratechange` event: `playbackRate` or `defaultPlaybackRate` changed.

\var umbra::player::RateChangeEvent::kType
The event type that selects this event in Player::AddEventListener().

\var umbra::player::RateChangeEvent::kName
The event name, `ratechange`, which the bindings use.

\var umbra::player::RateChangeEvent::state
The attribute values when the event was queued.

\struct umbra::player::ResizeEvent
The `resize` event: The intrinsic video dimensions became known or changed.

\var umbra::player::ResizeEvent::kType
The event type that selects this event in Player::AddEventListener().

\var umbra::player::ResizeEvent::kName
The event name, `resize`, which the bindings use.

\var umbra::player::ResizeEvent::state
The attribute values when the event was queued.

\struct umbra::player::VolumeChangeEvent
The `volumechange` event: `volume` or `muted` changed.

\var umbra::player::VolumeChangeEvent::kType
The event type that selects this event in Player::AddEventListener().

\var umbra::player::VolumeChangeEvent::kName
The event name, `volumechange`, which the bindings use.

\var umbra::player::VolumeChangeEvent::state
The attribute values when the event was queued.

\struct umbra::player::WarningEvent
The `warning` event: A degraded error that does not stop playback. WarningEvent::detail carries the ErrorDetail. Alumbra extension, not an HTML event.

\var umbra::player::WarningEvent::kType
The event type that selects this event in Player::AddEventListener().

\var umbra::player::WarningEvent::kName
The event name, `warning`, which the bindings use.

\var umbra::player::WarningEvent::state
The attribute values when the event was queued.

\var umbra::player::WarningEvent::detail
The degraded error, with its catalogue code, category, severity and redacted context.

\struct umbra::player::AudioTrackAddEvent
The audio list's `addtrack` event.

\var umbra::player::AudioTrackAddEvent::kType
The event type registered by Player::AddEventListener().

\var umbra::player::AudioTrackAddEvent::kName
The HTML event name, `addtrack`.

\var umbra::player::AudioTrackAddEvent::state
The player attributes when the event was queued.

\var umbra::player::AudioTrackAddEvent::track_id
Identifier of the track that was added.

\struct umbra::player::AudioTrackRemoveEvent
The audio list's `removetrack` event.

\var umbra::player::AudioTrackRemoveEvent::kType
The event type registered by Player::AddEventListener().

\var umbra::player::AudioTrackRemoveEvent::kName
The HTML event name, `removetrack`.

\var umbra::player::AudioTrackRemoveEvent::state
The player attributes when the event was queued.

\var umbra::player::AudioTrackRemoveEvent::track_id
Identifier of the track that was removed.

\struct umbra::player::AudioTracksChangeEvent
The audio list's `change` event, queued once when its enabled selection changes.

\var umbra::player::AudioTracksChangeEvent::kType
The event type registered by Player::AddEventListener().

\var umbra::player::AudioTracksChangeEvent::kName
The HTML event name, `change`.

\var umbra::player::AudioTracksChangeEvent::state
The player attributes when the event was queued.

\struct umbra::player::VideoTrackAddEvent
The video list's `addtrack` event.

\var umbra::player::VideoTrackAddEvent::kType
The event type registered by Player::AddEventListener().

\var umbra::player::VideoTrackAddEvent::kName
The HTML event name, `addtrack`.

\var umbra::player::VideoTrackAddEvent::state
The player attributes when the event was queued.

\var umbra::player::VideoTrackAddEvent::track_id
Identifier of the rendition that was added.

\struct umbra::player::VideoTrackRemoveEvent
The video list's `removetrack` event.

\var umbra::player::VideoTrackRemoveEvent::kType
The event type registered by Player::AddEventListener().

\var umbra::player::VideoTrackRemoveEvent::kName
The HTML event name, `removetrack`.

\var umbra::player::VideoTrackRemoveEvent::state
The player attributes when the event was queued.

\var umbra::player::VideoTrackRemoveEvent::track_id
Identifier of the rendition that was removed.

\struct umbra::player::VideoTracksChangeEvent
The video list's `change` event, queued once when its selected rendition changes.

\var umbra::player::VideoTracksChangeEvent::kType
The event type registered by Player::AddEventListener().

\var umbra::player::VideoTracksChangeEvent::kName
The HTML event name, `change`.

\var umbra::player::VideoTracksChangeEvent::state
The player attributes when the event was queued.

\struct umbra::player::TextTrackAddEvent
The text list's `addtrack` event, delivered from Player::DispatchEvents().

\var umbra::player::TextTrackAddEvent::kType
The event type registered by Player::AddEventListener().

\var umbra::player::TextTrackAddEvent::kName
The HTML event name, `addtrack`.

\var umbra::player::TextTrackAddEvent::state
The player attributes when the event was queued.

\struct umbra::player::TextTrackRemoveEvent
The text list's `removetrack` event, delivered from Player::DispatchEvents().

\var umbra::player::TextTrackRemoveEvent::kType
The event type registered by Player::AddEventListener().

\var umbra::player::TextTrackRemoveEvent::kName
The HTML event name, `removetrack`.

\var umbra::player::TextTrackRemoveEvent::state
The player attributes when the event was queued.

\struct umbra::player::TextTracksChangeEvent
The text list's `change` event, delivered from Player::DispatchEvents().

\var umbra::player::TextTracksChangeEvent::kType
The event type registered by Player::AddEventListener().

\var umbra::player::TextTracksChangeEvent::kName
The HTML event name, `change`.

\var umbra::player::TextTracksChangeEvent::state
The player attributes when the event was queued.

\class umbra::player::Subscription
An RAII handle that keeps a listener registered.

Destroying or cancelling the handle removes the listener, and a listener that was removed is not called again, even
for events that were already queued. The handle may outlive the player, and cancelling it then does nothing.

\fn umbra::player::Subscription::Subscription()
Creates an inactive handle.

\fn explicit umbra::player::Subscription::Subscription(std::function<void()> cancel)
Creates a handle that runs `cancel` once when it is cancelled or destroyed.

\fn umbra::player::Subscription::Subscription(Subscription&& other) noexcept
Takes over the listener of `other`, which becomes inactive.

\fn Subscription& umbra::player::Subscription::operator=(Subscription&& other) noexcept
Cancels the current listener and takes over the one of `other`.

\fn umbra::player::Subscription::~Subscription()
Cancels the listener.

\fn void umbra::player::Subscription::Cancel()
Removes the listener. Calling it again does nothing.

\fn bool umbra::player::Subscription::active() const
Returns whether the handle still holds a listener.
