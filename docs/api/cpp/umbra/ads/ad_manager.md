\file umbra/ads/ad_manager.h
The optional client-side ad schedule and provider handover contract.

\namespace umbra::ads
Types and interfaces for vendor-neutral ad scheduling.

The API uses C++20 and the installed Penumbra media-time types. Providers return
ad-specific results; failures do not use the content `MediaError` path.

\enum umbra::ads::OffsetKind
The anchor used to resolve an ad break against content duration.

\var umbra::ads::OffsetKind::kStart
The beginning of content, used for pre-roll.

\var umbra::ads::OffsetKind::kEnd
The end of content, used for post-roll.

\var umbra::ads::OffsetKind::kAbsolute
An offset in seconds from the beginning of content.

\var umbra::ads::OffsetKind::kPercentage
A percentage from zero through one hundred of content duration.

\struct umbra::ads::AdOffset
An offset kind and its numeric value. Start and end ignore the value.

\var umbra::ads::AdOffset::kind
The offset anchor.

\var umbra::ads::AdOffset::value
The offset in seconds or percentage, depending on kind.

\struct umbra::ads::Ad
One creative in a pod, including its duration, skip metadata and played state.

\var umbra::ads::Ad::id
The provider's stable creative identifier.

\var umbra::ads::Ad::duration
The creative duration, or unknown when the provider does not know it.

\var umbra::ads::Ad::skippable
Whether the creative can be skipped.

\var umbra::ads::Ad::skip_offset
The elapsed creative time at which skip becomes available.

\var umbra::ads::Ad::played
Whether this creative has completed or was ended by a provider failure.

\struct umbra::ads::AdPod
A sequence of creatives scheduled as one break.

\var umbra::ads::AdPod::ads
The creatives in playback order.

\var umbra::ads::AdPod::position
The zero-based active creative position.

\var umbra::ads::AdPod::size
The total number of creatives in the pod.

\enum umbra::ads::AdSourceKind
The type of ad response source attached to a break.

\var umbra::ads::AdSourceKind::kInlineVast
An inline VAST document.

\var umbra::ads::AdSourceKind::kAdTagUri
A URI that supplies an ad response.

\struct umbra::ads::AdSource
An inline VAST document or remote ad tag source.

\var umbra::ads::AdSource::kind
The source form.

\var umbra::ads::AdSource::uri
The inline XML or ad tag URI.

\var umbra::ads::AdSource::template_type
The response template expected from an ad tag URI.

\var umbra::ads::AdSource::allow_multiple_ads
Whether the source may return multiple ads.

\var umbra::ads::AdSource::follow_redirects
Whether redirects or wrappers may be followed.

\struct umbra::ads::AdBreakTrackingEvent
A tracking URI associated with an ad break lifecycle event.

\var umbra::ads::AdBreakTrackingEvent::event
The event name, such as `breakStart`, `breakEnd` or `error`.

\var umbra::ads::AdBreakTrackingEvent::uri
The URI dispatched when the event occurs.

\struct umbra::ads::AdBreak
A uniquely identified pod scheduled at a content offset.

\var umbra::ads::AdBreak::id
The stable break identifier.

\var umbra::ads::AdBreak::offset
The position at which the break is scheduled.

\var umbra::ads::AdBreak::scheduled_duration
The manifest or provider supplied break duration when known.

\var umbra::ads::AdBreak::pod
The creatives and playback position for this break.

\var umbra::ads::AdBreak::sources
The inline VAST documents or tag URIs attached to this break.

\var umbra::ads::AdBreak::tracking_events
The break start, break end and error tracking URIs.

\var umbra::ads::AdBreak::played
Whether the break has finished or was skipped by the seek policy.

\enum umbra::ads::SeekPolicy
The snapback behaviour for breaks crossed by a forward seek.

\var umbra::ads::SeekPolicy::kLast
Play the last crossed break.

\var umbra::ads::SeekPolicy::kAll
Play every crossed break in schedule order.

\var umbra::ads::SeekPolicy::kNone
Skip every crossed break.

\enum umbra::ads::ProviderStage
The provider lifecycle stage where an ad error occurred.

\var umbra::ads::ProviderStage::kPrepare
The provider failed while preparing a pod.

\var umbra::ads::ProviderStage::kStart
The provider failed while starting a creative.

\var umbra::ads::ProviderStage::kAdvance
The provider failed while reporting playback progress.

\var umbra::ads::ProviderStage::kTimeout
The provider did not advance before its configured timeout.

\struct umbra::ads::ContentSnapshot
The content position and selected tracks saved before an ad break.

\var umbra::ads::ContentSnapshot::position
The content media time to restore after the break.

\var umbra::ads::ContentSnapshot::selected_tracks
The selected content track identifiers to restore after the break.

\struct umbra::ads::AdErrorEvent
An error reported on the ad event path.

\var umbra::ads::AdErrorEvent::break_id
The break that encountered an error.

\var umbra::ads::AdErrorEvent::ad_id
The creative that encountered an error, if one had started.

\var umbra::ads::AdErrorEvent::code
The catalogue classification `ErrorCode::kAdsUnavailable`. It stays on the ad event path and is not converted to `MediaError`.

\var umbra::ads::AdErrorEvent::stage
The provider lifecycle stage where the error occurred.

\var umbra::ads::AdErrorEvent::provider_code
The provider's error code, when supplied.

\var umbra::ads::AdErrorEvent::message
The provider's diagnostic message.

\struct umbra::ads::ProviderResult
The outcome of preparing or starting a creative.

\var umbra::ads::ProviderResult::ok
Whether the provider operation succeeded.

\var umbra::ads::ProviderResult::provider_code
The provider's error code when the operation failed.

\var umbra::ads::ProviderResult::message
The provider's error message when the operation failed.

\struct umbra::ads::ProviderProgress
The outcome of one provider progress update.

\var umbra::ads::ProviderProgress::ok
Whether the progress request succeeded.

\var umbra::ads::ProviderProgress::complete
Whether the active creative completed.

\var umbra::ads::ProviderProgress::provider_code
The provider's error code when the progress request failed.

\var umbra::ads::ProviderProgress::message
The provider's error message when the progress request failed.

\struct umbra::ads::ScheduleResult
The outcome of validating and installing a break schedule.

\var umbra::ads::ScheduleResult::ok
Whether the schedule was accepted.

\var umbra::ads::ScheduleResult::message
The validation message when the schedule was rejected.

\class umbra::ads::ContentController
The manager's interface to content playback. The manager alone calls pause,
snapshot, seek, resume and deferred ended operations around ad breaks.

\fn virtual void umbra::ads::ContentController::BeginContent()
Starts content after any pre-roll has finished.

\fn virtual umbra::ads::ContentSnapshot umbra::ads::ContentController::Snapshot() const
Captures the content position and selected tracks.

\fn virtual void umbra::ads::ContentController::PauseContent()
Pauses content for an ad break.

\fn virtual void umbra::ads::ContentController::SeekContent(penumbra::MediaTime position)
Seeks content before snapback ads play.

\fn virtual void umbra::ads::ContentController::ResumeContent(const ContentSnapshot& snapshot)
Restores content at the saved position with the saved selected tracks.

\fn virtual void umbra::ads::ContentController::DispatchEnded()
Dispatches the content ended notification after post-rolls finish.

\class umbra::ads::AdProvider
The provider lifecycle used by the scheduler.

\fn virtual ProviderResult umbra::ads::AdProvider::Prepare(const AdBreak& ad_break)
Prepares the pod. Failure ends the break and restores content.

\fn virtual ProviderResult umbra::ads::AdProvider::Start(const Ad& ad)
Starts one creative. Failure ends the break and restores content.

\fn virtual ProviderProgress umbra::ads::AdProvider::Advance(penumbra::MediaTime now)
Reports provider progress using the supplied media-clock time.

\struct umbra::ads::AdManagerOptions
Configuration for seek snapback, replay and provider timeout.

\var umbra::ads::AdManagerOptions::seek_policy
The policy used when a forward seek crosses several breaks.

\var umbra::ads::AdManagerOptions::replay_played_breaks
Whether completed or skipped breaks can be selected again.

\var umbra::ads::AdManagerOptions::provider_timeout
The maximum provider inactivity duration before the break ends.

\var umbra::ads::AdManagerOptions::prefetch_lead
The content time before a break when its source should be requested. Initialise
it from `ads.prefetch_lead_ms` when applying settings.

\var umbra::ads::AdManagerOptions::on_prefetch
The callback used to request sources when they enter the prefetch window.

\var umbra::ads::AdManagerOptions::on_tracking_event
The callback used to dispatch the event name and matching break tracking URI.

\class umbra::ads::AdManager
Schedules pre-roll, mid-roll and post-roll breaks and coordinates content handover.

\fn umbra::ads::AdManager::AdManager(AdManagerOptions options, ContentController& content, AdProvider& provider, std::function<void(const AdErrorEvent&)> on_ad_error)
Creates a scheduler over a content controller and one provider.

\fn umbra::ads::AdManager::~AdManager()
Destroys the scheduler.

\fn ScheduleResult umbra::ads::AdManager::SetBreaks(std::vector<AdBreak> breaks, penumbra::TimeDelta content_duration)
Validates break identifiers and offsets, resolves cue points, and installs the schedule.

\fn void umbra::ads::AdManager::StartContent(penumbra::MediaTime now)
Starts content after any pre-roll break.

\fn void umbra::ads::AdManager::OnContentTime(penumbra::MediaTime previous, penumbra::MediaTime current, penumbra::MediaTime now)
Starts pending mid-rolls crossed by the content clock.

\fn void umbra::ads::AdManager::Seek(penumbra::MediaTime previous, penumbra::MediaTime target, penumbra::MediaTime now)
Seeks content and applies the configured snapback policy to crossed breaks.

\fn void umbra::ads::AdManager::ContentEnded(penumbra::MediaTime now)
Schedules post-rolls and defers the content ended notification.

\fn void umbra::ads::AdManager::Tick(penumbra::MediaTime now)
Advances provider state and checks its timeout against the supplied media clock.

\fn bool umbra::ads::AdManager::InAdBreak() const
Returns whether a client-side break is active.

\fn bool umbra::ads::AdManager::EndedPending() const
Returns whether the ended notification is waiting for post-roll completion.

\fn std::vector<penumbra::MediaTime> umbra::ads::AdManager::CuePoints() const
Returns resolved cue points for seek-bar markers.

\fn std::optional<penumbra::MediaTime> umbra::ads::ResolveOffset(const AdOffset& offset, penumbra::TimeDelta content_duration)
Resolves a supported offset against finite content duration, or returns empty for invalid input.
