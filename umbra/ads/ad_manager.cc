#include "umbra/ads/ad_manager.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "penumbra/interface/media_time.h"
#include "umbra/ads/manifest_bridge.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/manifest/presentation.h"

namespace umbra::ads {
namespace {

bool IsFiniteDuration(penumbra::TimeDelta duration) {
  return duration.is_finite() && duration >= penumbra::TimeDelta::Zero();
}

}

std::optional<penumbra::MediaTime> ResolveOffset(
    const AdOffset& offset, penumbra::TimeDelta content_duration) {
  if (!IsFiniteDuration(content_duration)) {
    return std::nullopt;
  }
  switch (offset.kind) {
    case OffsetKind::kStart:
      return penumbra::MediaTime::Zero();
    case OffsetKind::kEnd:
      return penumbra::MediaTime::FromMicroseconds(
          content_duration.InMicroseconds());
    case OffsetKind::kAbsolute: {
      if (!std::isfinite(offset.value) || offset.value < 0.0 ||
          offset.value >
              static_cast<double>(std::numeric_limits<int64_t>::max()) /
                  1'000'000.0) {
        return std::nullopt;
      }
      return penumbra::MediaTime::FromMicroseconds(
          static_cast<int64_t>(std::llround(offset.value * 1'000'000.0)));
    }
    case OffsetKind::kPercentage: {
      if (!std::isfinite(offset.value) || offset.value < 0.0 ||
          offset.value > 100.0) {
        return std::nullopt;
      }
      const long double micros =
          static_cast<long double>(content_duration.InMicroseconds()) *
          static_cast<long double>(offset.value) / 100.0L;
      if (micros >
          static_cast<long double>(std::numeric_limits<int64_t>::max())) {
        return std::nullopt;
      }
      return penumbra::MediaTime::FromMicroseconds(
          static_cast<int64_t>(std::llround(micros)));
    }
  }
  return std::nullopt;
}

std::optional<AdBreak> FromManifestCue(const umbra::manifest::AdBreak& cue) {
  if (cue.id.empty() || !cue.start.is_finite()) {
    return std::nullopt;
  }
  AdBreak ad_break;
  ad_break.id = cue.id;
  ad_break.offset = {
      .kind = OffsetKind::kAbsolute,
      .value = static_cast<double>(cue.start.InMicroseconds()) / 1'000'000.0};
  if (cue.duration.is_finite()) {
    ad_break.scheduled_duration = cue.duration;
  }
  return ad_break;
}

AdManager::AdManager(AdManagerOptions options, ContentController& content,
                     AdProvider& provider,
                     std::function<void(const AdErrorEvent&)> on_ad_error)
    : options_(options),
      content_(content),
      provider_(provider),
      on_ad_error_(std::move(on_ad_error)) {}

AdManager::~AdManager() = default;

ScheduleResult AdManager::SetBreaks(std::vector<AdBreak> breaks,
                                    penumbra::TimeDelta content_duration) {
  if (!IsFiniteDuration(content_duration)) {
    return {.ok = false, .message = "content duration must be finite"};
  }
  std::set<std::string> ids;
  std::vector<ScheduledBreak> scheduled;
  scheduled.reserve(breaks.size());
  for (AdBreak& ad_break : breaks) {
    if (ad_break.id.empty() || !ids.insert(ad_break.id).second) {
      return {.ok = false, .message = "ad break ids must be unique"};
    }
    const std::optional<penumbra::MediaTime> time =
        ResolveOffset(ad_break.offset, content_duration);
    if (!time || *time > penumbra::MediaTime::FromMicroseconds(
                             content_duration.InMicroseconds())) {
      return {.ok = false, .message = "ad break offset is invalid"};
    }
    if (ad_break.pod.size == 0) {
      ad_break.pod.size = ad_break.pod.ads.size();
    }
    for (const Ad& ad : ad_break.pod.ads) {
      if (ad.id.empty() ||
          (ad.duration.is_finite() &&
           ad.duration < penumbra::TimeDelta::Zero()) ||
          (ad.skip_offset.is_finite() &&
           ad.skip_offset < penumbra::TimeDelta::Zero())) {
        return {.ok = false, .message = "ad metadata is invalid"};
      }
    }
    scheduled.push_back({std::move(ad_break), *time});
  }
  std::ranges::stable_sort(
      scheduled, [](const ScheduledBreak& left, const ScheduledBreak& right) {
        return left.time < right.time;
      });
  breaks_ = std::move(scheduled);
  content_duration_ = content_duration;
  queued_.clear();
  active_break_.reset();
  return {};
}

void AdManager::StartContent(penumbra::MediaTime now) {
  last_now_ = now;
  if (content_started_) {
    return;
  }
  content_started_ = true;
  for (std::size_t index = 0; index < breaks_.size(); ++index) {
    if (breaks_[index].time == penumbra::MediaTime::Zero() &&
        !IsPlayed(breaks_[index])) {
      queued_.push_back(index);
    }
  }
  if (queued_.empty()) {
    content_.BeginContent();
  } else {
    content_start_pending_ = true;
    PlayQueued(now);
  }
}

void AdManager::OnContentTime(penumbra::MediaTime previous,
                              penumbra::MediaTime current,
                              penumbra::MediaTime now) {
  last_now_ = now;
  if (!content_started_ || active_break_ || current < previous) {
    return;
  }
  for (std::size_t index = 0; index < breaks_.size(); ++index) {
    const ScheduledBreak& scheduled = breaks_[index];
    if (scheduled.time > previous && scheduled.time <= current &&
        scheduled.time != penumbra::MediaTime::FromMicroseconds(
                              content_duration_.InMicroseconds()) &&
        !IsPlayed(scheduled)) {
      queued_.push_back(index);
    }
  }
  PlayQueued(now);
}

void AdManager::QueueCrossed(penumbra::MediaTime previous,
                             penumbra::MediaTime target) {
  std::vector<std::size_t> crossed;
  for (std::size_t index = 0; index < breaks_.size(); ++index) {
    if (breaks_[index].time > previous && breaks_[index].time <= target &&
        !IsPlayed(breaks_[index])) {
      crossed.push_back(index);
    }
  }
  std::vector<std::size_t> selected;
  if (options_.seek_policy == SeekPolicy::kAll) {
    selected = crossed;
  } else if (options_.seek_policy == SeekPolicy::kLast && !crossed.empty()) {
    selected.push_back(crossed.back());
  }
  for (const std::size_t index : crossed) {
    const bool selected_break =
        std::ranges::find(selected, index) != selected.end();
    if (!selected_break) {
      breaks_[index].ad_break.played = true;
    }
  }
  queued_.insert(queued_.end(), selected.begin(), selected.end());
}

void AdManager::Seek(penumbra::MediaTime previous, penumbra::MediaTime target,
                     penumbra::MediaTime now) {
  last_now_ = now;
  if (target < previous) {
    content_.SeekContent(target);
    return;
  }
  QueueCrossed(previous, target);
  content_.SeekContent(target);
  PlayQueued(now);
}

void AdManager::ContentEnded(penumbra::MediaTime now) {
  last_now_ = now;
  if (content_ended_) {
    return;
  }
  content_ended_ = true;
  ended_pending_ = true;
  if (!active_break_ && !post_rolls_queued_) {
    for (std::size_t index = 0; index < breaks_.size(); ++index) {
      if (breaks_[index].time == penumbra::MediaTime::FromMicroseconds(
                                     content_duration_.InMicroseconds()) &&
          !IsPlayed(breaks_[index])) {
        queued_.push_back(index);
      }
    }
    post_rolls_queued_ = true;
    PlayQueued(now);
  }
}

void AdManager::Tick(penumbra::MediaTime now) {
  last_now_ = now;
  if (!active_break_) {
    PlayQueued(now);
    return;
  }
  if (stage_started_.is_finite() &&
      IsFiniteDuration(options_.provider_timeout) &&
      now - stage_started_ >= options_.provider_timeout) {
    FailBreak(ProviderStage::kTimeout, {.ok = false,
                                        .provider_code = "timeout",
                                        .message = "ad provider timed out"});
    return;
  }
  const AdBreak& ad_break = breaks_[*active_break_].ad_break;
  if (active_ad_ >= ad_break.pod.ads.size()) {
    FinishBreak();
    return;
  }
  const ProviderProgress progress = provider_.Advance(now);
  if (!progress.ok) {
    FailBreak(ProviderStage::kAdvance, {.ok = false,
                                        .provider_code = progress.provider_code,
                                        .message = progress.message});
    return;
  }
  if (progress.complete) {
    FinishAd(now);
  }
}

bool AdManager::InAdBreak() const { return active_break_.has_value(); }

bool AdManager::EndedPending() const { return ended_pending_; }

std::vector<penumbra::MediaTime> AdManager::CuePoints() const {
  std::vector<penumbra::MediaTime> cue_points;
  cue_points.reserve(breaks_.size());
  for (const ScheduledBreak& scheduled : breaks_) {
    cue_points.push_back(scheduled.time);
  }
  return cue_points;
}

bool AdManager::BeginBreak(std::size_t index, penumbra::MediaTime now) {
  if (index >= breaks_.size() || breaks_[index].ad_break.pod.ads.empty()) {
    if (index < breaks_.size()) {
      breaks_[index].ad_break.played = true;
    }
    return false;
  }
  active_break_ = index;
  active_ad_ = 0;
  breaks_[index].ad_break.pod.position = 0;
  snapshot_ = content_.Snapshot();
  content_.PauseContent();
  stage_started_ = now;
  const AdBreak& ad_break = breaks_[index].ad_break;
  const ProviderResult prepare_status = provider_.Prepare(ad_break);
  if (!prepare_status.ok) {
    FailBreak(ProviderStage::kPrepare, prepare_status);
    return true;
  }
  const ProviderResult start_status = provider_.Start(ad_break.pod.ads.front());
  if (!start_status.ok) {
    FailBreak(ProviderStage::kStart, start_status);
    return true;
  }
  return true;
}

void AdManager::FinishBreak() {
  if (!active_break_) {
    return;
  }
  breaks_[*active_break_].ad_break.played = true;
  if (snapshot_) {
    content_.ResumeContent(*snapshot_);
  }
  snapshot_.reset();
  active_break_.reset();
  if (content_ended_ && !post_rolls_queued_) {
    for (std::size_t index = 0; index < breaks_.size(); ++index) {
      if (breaks_[index].time == penumbra::MediaTime::FromMicroseconds(
                                     content_duration_.InMicroseconds()) &&
          !IsPlayed(breaks_[index])) {
        queued_.push_back(index);
      }
    }
    post_rolls_queued_ = true;
  }
  if (queued_.empty() && ended_pending_) {
    ended_pending_ = false;
    content_.DispatchEnded();
  } else {
    PlayQueued(last_now_);
  }
}

void AdManager::FailBreak(ProviderStage stage, const ProviderResult& status) {
  if (!active_break_) {
    return;
  }
  const AdBreak& ad_break = breaks_[*active_break_].ad_break;
  const std::string ad_id = active_ad_ < ad_break.pod.ads.size()
                                ? ad_break.pod.ads[active_ad_].id
                                : std::string();
  if (on_ad_error_) {
    on_ad_error_(AdErrorEvent{.break_id = ad_break.id,
                              .ad_id = ad_id,
                              .code = umbra::base::ErrorCode::kAdsUnavailable,
                              .stage = stage,
                              .provider_code = status.provider_code,
                              .message = status.message});
  }
  FinishBreak();
}

void AdManager::FinishAd(penumbra::MediaTime now) {
  if (!active_break_) {
    return;
  }
  AdBreak& ad_break = breaks_[*active_break_].ad_break;
  if (active_ad_ < ad_break.pod.ads.size()) {
    ad_break.pod.ads[active_ad_].played = true;
  }
  ++active_ad_;
  ad_break.pod.position = active_ad_;
  if (active_ad_ >= ad_break.pod.ads.size()) {
    FinishBreak();
    return;
  }
  stage_started_ = now;
  const ProviderResult status = provider_.Start(ad_break.pod.ads[active_ad_]);
  if (!status.ok) {
    FailBreak(ProviderStage::kStart, status);
  }
}

void AdManager::PlayQueued(penumbra::MediaTime now) {
  while (!active_break_ && !queued_.empty()) {
    const std::size_t index = queued_.front();
    queued_.erase(queued_.begin());
    if (IsPlayed(breaks_[index])) {
      continue;
    }
    if (BeginBreak(index, now)) {
      return;
    }
  }
  if (queued_.empty() && ended_pending_ && !active_break_) {
    ended_pending_ = false;
    content_.DispatchEnded();
  } else if (queued_.empty() && content_start_pending_ && !active_break_) {
    content_start_pending_ = false;
    content_.BeginContent();
  }
}

bool AdManager::IsPlayed(const ScheduledBreak& scheduled) const {
  if (options_.replay_played_breaks) {
    return false;
  }
  return scheduled.ad_break.played;
}

}
