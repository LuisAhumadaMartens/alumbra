\file umbra/ads/provider.h
Capability negotiation, consent forwarding and lifecycle operations for ad providers.

\struct umbra::ads::ProviderCapabilities
The provider behaviour that the AdManager must account for.

\var umbra::ads::ProviderCapabilities::renders_own_ui
Whether the provider draws its own ad interface.

\var umbra::ads::ProviderCapabilities::owns_skip_control
Whether the provider draws and handles the skip control.

\var umbra::ads::ProviderCapabilities::fires_own_beacons
Whether the provider sends tracking beacons itself.

\var umbra::ads::ProviderCapabilities::supports_preloading
Whether the provider can prepare ads before their break starts.

\var umbra::ads::ProviderCapabilities::needs_content_video_element
Whether the provider requires the content video element.

\struct umbra::ads::AdConsent
Consent signals supplied by the integrator. Values are forwarded unchanged and are not interpreted or logged.

\var umbra::ads::AdConsent::tcf_string
The original TCF consent string.

\var umbra::ads::AdConsent::gpp_string
The original GPP consent string.

\var umbra::ads::AdConsent::limited_ads_enabled
The limited-ads signal.

\var umbra::ads::AdConsent::child_directed_enabled
The child-directed treatment signal.

\struct umbra::ads::AdRequest
An ad tag request and the consent signals to pass to its provider.

\var umbra::ads::AdRequest::ad_tag_url
The ad tag URL.

\var umbra::ads::AdRequest::consent
The consent values associated with this request.

\struct umbra::ads::AdSize
The requested ad viewport size in pixels.

\var umbra::ads::AdSize::width
The viewport width in pixels.

\var umbra::ads::AdSize::height
The viewport height in pixels.

\struct umbra::ads::AdProviderEvent
A normalised provider event and the pod and creative metadata available for it.

\enum umbra::ads::AdProviderEvent::Type
The public lifecycle and interaction event names.

\var umbra::ads::AdProviderEvent::Type::kAdsLoaded
The provider finished loading the requested ads.

\var umbra::ads::AdProviderEvent::Type::kAdBreakStart
An ad break began.

\var umbra::ads::AdProviderEvent::Type::kAdStart
A creative began playback.

\var umbra::ads::AdProviderEvent::Type::kAdProgress
Playback progress was reported for the active creative.

\var umbra::ads::AdProviderEvent::Type::kAdQuartile
A creative crossed a quartile boundary.

\var umbra::ads::AdProviderEvent::Type::kAdClick
The viewer activated the creative click-through.

\var umbra::ads::AdProviderEvent::Type::kAdSkippableStateChange
The creative's skip availability changed.

\var umbra::ads::AdProviderEvent::Type::kAdSkip
The active creative was skipped.

\var umbra::ads::AdProviderEvent::Type::kAdComplete
The active creative completed.

\var umbra::ads::AdProviderEvent::Type::kAdBreakEnd
The current ad break ended.

\var umbra::ads::AdProviderEvent::Type::kAllAdsCompleted
All requested ads completed.

\var umbra::ads::AdProviderEvent::Type::kAdError
An ad operation failed without changing the content media-error state.

\var umbra::ads::AdProviderEvent::type
The event name.

\var umbra::ads::AdProviderEvent::break_id
The associated break identifier, when known.

\var umbra::ads::AdProviderEvent::ad_id
The associated ad identifier, when known.

\var umbra::ads::AdProviderEvent::creative_id
The provider creative identifier, when known.

\var umbra::ads::AdProviderEvent::pod_position
The zero-based creative position in the pod.

\var umbra::ads::AdProviderEvent::pod_size
The number of creatives in the pod.

\var umbra::ads::AdProviderEvent::quartile
The one-based quartile number for a quartile event.

\var umbra::ads::AdProviderEvent::skippable
Whether the active creative currently allows skipping.

\var umbra::ads::AdProviderEvent::duration
The creative duration, or unknown when unavailable.

\var umbra::ads::AdProviderEvent::current_time
The elapsed creative time, or unknown when unavailable.

\var umbra::ads::AdProviderEvent::skip_offset
The elapsed time at which skipping becomes available, or unknown when unavailable.

\var umbra::ads::AdProviderEvent::provider_code
The provider-specific error code, when supplied.

\typedef umbra::ads::AdProviderEventCallback
Receives one normalised ad event.

\class umbra::ads::ProviderLifecycle
The capability-negotiating provider interface. Request, playback, UI and content lifecycle calls are owned by the AdManager.

\fn virtual umbra::ads::ProviderLifecycle::~ProviderLifecycle()
Destroys the provider interface.

\fn virtual ProviderCapabilities umbra::ads::ProviderLifecycle::Capabilities() const
Returns the provider capabilities.

\fn virtual ProviderResult umbra::ads::ProviderLifecycle::RequestAds(const AdRequest& request)
Requests ads and receives consent unchanged.

\fn virtual ProviderResult umbra::ads::ProviderLifecycle::StartAd(const Ad& ad)
Starts a creative.

\fn virtual ProviderResult umbra::ads::ProviderLifecycle::PauseAd()
Pauses the active creative.

\fn virtual ProviderResult umbra::ads::ProviderLifecycle::ResumeAd()
Resumes the active creative.

\fn virtual ProviderResult umbra::ads::ProviderLifecycle::SkipAd()
Skips the active creative when allowed.

\fn virtual ProviderResult umbra::ads::ProviderLifecycle::Resize(AdSize size)
Updates the provider viewport size.

\fn virtual void umbra::ads::ProviderLifecycle::ContentProgress(penumbra::MediaTime position)
Reports content progress for providers that schedule from the content playhead.

\fn virtual void umbra::ads::ProviderLifecycle::ContentComplete()
Notifies the provider that content has ended.

\fn virtual void umbra::ads::ProviderLifecycle::SetEventCallback(AdProviderEventCallback callback)
Sets the callback for normalised provider events.

\fn virtual void umbra::ads::ProviderLifecycle::Destroy()
Releases provider resources and ignores later lifecycle calls.

\class umbra::ads::BuiltInAdProvider
The built-in provider lifecycle adapter. Its request handler is supplied by the built-in ad implementation.

\typedef umbra::ads::BuiltInAdProvider::RequestHandler
Receives a request and fills its resulting break list.

\fn umbra::ads::BuiltInAdProvider::BuiltInAdProvider(RequestHandler request_handler)
Creates the provider with the built-in ad request implementation.

\fn ProviderCapabilities umbra::ads::BuiltInAdProvider::Capabilities() const
Reports player-rendered ads, player-owned skip controls and preloading support.

\fn ProviderResult umbra::ads::BuiltInAdProvider::RequestAds(const AdRequest& request)
Loads breaks through the supplied request handler and emits `adsloaded` on success.

\fn ProviderResult umbra::ads::BuiltInAdProvider::StartAd(const Ad& ad)
Starts a creative and emits `adstart`.

\fn ProviderResult umbra::ads::BuiltInAdProvider::PauseAd()
Pauses the active creative.

\fn ProviderResult umbra::ads::BuiltInAdProvider::ResumeAd()
Resumes the active creative.

\fn ProviderResult umbra::ads::BuiltInAdProvider::SkipAd()
Emits `adskip` for the active creative.

\fn ProviderResult umbra::ads::BuiltInAdProvider::Resize(AdSize size)
Validates a non-negative viewport size.

\fn void umbra::ads::BuiltInAdProvider::ContentProgress(penumbra::MediaTime position)
Receives content progress for providers that schedule from the content playhead.

\fn void umbra::ads::BuiltInAdProvider::ContentComplete()
Emits `allAdsCompleted`.

\fn void umbra::ads::BuiltInAdProvider::SetEventCallback(AdProviderEventCallback callback)
Sets the normalised event callback.

\fn void umbra::ads::BuiltInAdProvider::Destroy()
Clears loaded breaks and the event callback.

\fn void umbra::ads::BuiltInAdProvider::ReportProgress(penumbra::TimeDelta elapsed)
Emits ad progress and each newly crossed 25, 50 or 75 per cent quartile.

\fn void umbra::ads::BuiltInAdProvider::ReportClick()
Emits a click event for the active creative.

\fn void umbra::ads::BuiltInAdProvider::SetSkippable(bool skippable)
Emits a skip-state change when the active creative's availability changes.

\fn void umbra::ads::BuiltInAdProvider::CompleteAd()
Emits completion for the active creative.

\fn void umbra::ads::BuiltInAdProvider::EndBreak()
Emits the end of the active break.

\fn void umbra::ads::BuiltInAdProvider::ReportError(std::string provider_code)
Emits the provider error and ends an active break.

\fn const std::vector<AdBreak>& umbra::ads::BuiltInAdProvider::Breaks() const
Returns the loaded break list.
