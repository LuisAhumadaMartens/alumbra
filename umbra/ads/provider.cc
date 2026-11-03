#include "umbra/ads/provider.h"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "penumbra/interface/media_time.h"
#include "umbra/ads/ad_manager.h"

namespace umbra::ads {

BuiltInAdProvider::BuiltInAdProvider(RequestHandler request_handler)
    : request_handler_(std::move(request_handler)) {}

ProviderCapabilities BuiltInAdProvider::Capabilities() const {
  return {.renders_own_ui = false,
          .owns_skip_control = false,
          .fires_own_beacons = false,
          .supports_preloading = true,
          .needs_content_video_element = false};
}

ProviderResult BuiltInAdProvider::RequestAds(const AdRequest& request) {
  if (destroyed_) {
    return {.ok = false,
            .provider_code = "destroyed",
            .message = "provider has been destroyed"};
  }
  if (!request_handler_) {
    return {.ok = false,
            .provider_code = "request_unavailable",
            .message = "ad request handler is unavailable"};
  }
  std::vector<AdBreak> requested_breaks;
  ProviderResult result = request_handler_(request, requested_breaks);
  if (!result.ok) {
    AdProviderEvent event = Event(AdProviderEvent::Type::kAdError);
    event.provider_code = result.provider_code;
    Emit(event);
    return result;
  }
  breaks_ = std::move(requested_breaks);
  const AdProviderEvent event = Event(AdProviderEvent::Type::kAdsLoaded);
  Emit(event);
  return result;
}

ProviderResult BuiltInAdProvider::StartAd(const Ad& ad) {
  if (destroyed_) {
    return {.ok = false,
            .provider_code = "destroyed",
            .message = "provider has been destroyed"};
  }
  active_ad_ = ad;
  active_break_ = true;
  active_break_id_ = breaks_.empty() ? std::string() : breaks_.front().id;
  active_pod_position_ = 0;
  active_pod_size_ = breaks_.empty() ? 1 : breaks_.front().pod.size;
  if (!breaks_.empty()) {
    const std::vector<Ad>& pod_ads = breaks_.front().pod.ads;
    for (std::size_t index = 0; index < pod_ads.size(); ++index) {
      if (pod_ads[index].id == ad.id) {
        active_pod_position_ = index;
        break;
      }
    }
  }
  last_quartile_ = 0;
  skippable_ = ad.skippable;
  const AdProviderEvent break_event =
      Event(AdProviderEvent::Type::kAdBreakStart);
  Emit(break_event);
  const AdProviderEvent event = Event(AdProviderEvent::Type::kAdStart);
  Emit(event);
  AdProviderEvent skip_state_event =
      Event(AdProviderEvent::Type::kAdSkippableStateChange);
  skip_state_event.skippable = skippable_;
  Emit(skip_state_event);
  return {};
}

ProviderResult BuiltInAdProvider::PauseAd() { return {}; }

ProviderResult BuiltInAdProvider::ResumeAd() { return {}; }

ProviderResult BuiltInAdProvider::SkipAd() {
  if (destroyed_) {
    return {.ok = false,
            .provider_code = "destroyed",
            .message = "provider has been destroyed"};
  }
  if (!active_ad_) {
    return {.ok = false,
            .provider_code = "no_active_ad",
            .message = "there is no active ad to skip"};
  }
  const AdProviderEvent event = Event(AdProviderEvent::Type::kAdSkip);
  Emit(event);
  active_ad_.reset();
  return {};
}

ProviderResult BuiltInAdProvider::Resize(AdSize size) {
  if (size.width < 0 || size.height < 0) {
    return {.ok = false,
            .provider_code = "invalid_size",
            .message = "ad size must be non-negative"};
  }
  return {};
}

void BuiltInAdProvider::ContentProgress(penumbra::MediaTime position) {
  static_cast<void>(position);
}

void BuiltInAdProvider::ContentComplete() {
  if (!destroyed_) {
    const AdProviderEvent event =
        Event(AdProviderEvent::Type::kAllAdsCompleted);
    Emit(event);
  }
}

void BuiltInAdProvider::SetEventCallback(AdProviderEventCallback callback) {
  event_callback_ = std::move(callback);
}

void BuiltInAdProvider::Destroy() {
  destroyed_ = true;
  active_ad_.reset();
  active_break_ = false;
  breaks_.clear();
  event_callback_ = {};
}

void BuiltInAdProvider::ReportProgress(penumbra::TimeDelta elapsed) {
  if (destroyed_ || !active_ad_ || !elapsed.is_finite() ||
      elapsed < penumbra::TimeDelta::Zero()) {
    return;
  }
  AdProviderEvent progress = Event(AdProviderEvent::Type::kAdProgress);
  progress.current_time = elapsed;
  Emit(progress);
  if (!active_ad_->duration.is_finite() ||
      active_ad_->duration <= penumbra::TimeDelta::Zero()) {
    return;
  }
  for (std::uint8_t quartile = 1; quartile <= 3; ++quartile) {
    const std::int64_t duration_us = active_ad_->duration.InMicroseconds();
    const std::int64_t threshold =
        ((duration_us / 4) * quartile) + (((duration_us % 4) * quartile) / 4);
    if (quartile <= last_quartile_ || elapsed.InMicroseconds() < threshold) {
      continue;
    }
    AdProviderEvent event = Event(AdProviderEvent::Type::kAdQuartile);
    event.current_time = elapsed;
    event.quartile = quartile;
    Emit(event);
    last_quartile_ = quartile;
  }
}

void BuiltInAdProvider::ReportClick() {
  if (!destroyed_ && active_ad_) {
    const AdProviderEvent event = Event(AdProviderEvent::Type::kAdClick);
    Emit(event);
  }
}

void BuiltInAdProvider::SetSkippable(bool skippable) {
  if (destroyed_ || !active_ad_ || skippable_ == skippable) {
    return;
  }
  skippable_ = skippable;
  AdProviderEvent event = Event(AdProviderEvent::Type::kAdSkippableStateChange);
  event.skippable = skippable_;
  Emit(event);
}

void BuiltInAdProvider::CompleteAd() {
  if (!destroyed_ && active_ad_) {
    const AdProviderEvent event = Event(AdProviderEvent::Type::kAdComplete);
    Emit(event);
    active_ad_.reset();
  }
}

void BuiltInAdProvider::EndBreak() {
  if (!destroyed_ && active_break_) {
    const AdProviderEvent event = Event(AdProviderEvent::Type::kAdBreakEnd);
    Emit(event);
    active_break_ = false;
    active_ad_.reset();
  }
}

void BuiltInAdProvider::ReportError(std::string provider_code) {
  if (!destroyed_) {
    AdProviderEvent event = Event(AdProviderEvent::Type::kAdError);
    event.provider_code = std::move(provider_code);
    Emit(event);
    EndBreak();
  }
}

const std::vector<AdBreak>& BuiltInAdProvider::Breaks() const {
  return breaks_;
}

void BuiltInAdProvider::Emit(const AdProviderEvent& event) const {
  if (event_callback_) {
    event_callback_(event);
  }
}

AdProviderEvent BuiltInAdProvider::Event(AdProviderEvent::Type type) const {
  AdProviderEvent event;
  event.type = type;
  event.break_id = active_break_id_;
  if (active_ad_) {
    event.ad_id = active_ad_->id;
    event.creative_id = active_ad_->id;
    event.pod_position = active_pod_position_;
    event.pod_size = active_pod_size_;
    event.duration = active_ad_->duration;
    event.skip_offset = active_ad_->skip_offset;
  }
  return event;
}

}
