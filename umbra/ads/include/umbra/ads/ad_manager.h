#ifndef UMBRA_ADS_AD_MANAGER_H_
#define UMBRA_ADS_AD_MANAGER_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "penumbra/interface/media_time.h"
#include "umbra/base/error_catalogue.h"

namespace umbra::ads {

enum class OffsetKind { kStart, kEnd, kAbsolute, kPercentage };

struct AdOffset {
  OffsetKind kind = OffsetKind::kStart;
  double value = 0.0;
};

struct Ad {
  std::string id;
  penumbra::TimeDelta duration = penumbra::TimeDelta::Unknown();
  bool skippable = false;
  penumbra::TimeDelta skip_offset = penumbra::TimeDelta::Unknown();
  bool played = false;
};

struct AdPod {
  std::vector<Ad> ads;
  std::size_t position = 0;
  std::size_t size = 0;
};

enum class AdSourceKind { kInlineVast, kAdTagUri };

struct AdSource {
  AdSourceKind kind = AdSourceKind::kInlineVast;
  std::string uri;
  std::string template_type;
  bool allow_multiple_ads = false;
  bool follow_redirects = true;
};

struct AdBreakTrackingEvent {
  std::string event;
  std::string uri;
};

struct AdBreak {
  std::string id;
  AdOffset offset;
  std::optional<penumbra::TimeDelta> scheduled_duration;
  AdPod pod;
  std::vector<AdSource> sources;
  std::vector<AdBreakTrackingEvent> tracking_events;
  bool played = false;
};

enum class SeekPolicy { kLast, kAll, kNone };
enum class ProviderStage { kPrepare, kStart, kAdvance, kTimeout };

struct ContentSnapshot {
  penumbra::MediaTime position = penumbra::MediaTime::Unknown();
  std::vector<std::string> selected_tracks;
};

struct AdErrorEvent {
  std::string break_id;
  std::string ad_id;
  umbra::base::ErrorCode code = umbra::base::ErrorCode::kAdsUnavailable;
  ProviderStage stage = ProviderStage::kPrepare;
  std::string provider_code;
  std::string message;
};

struct ProviderResult {
  bool ok = true;
  std::string provider_code;
  std::string message;
};

struct ProviderProgress {
  bool ok = true;
  bool complete = false;
  std::string provider_code;
  std::string message;
};

struct ScheduleResult {
  bool ok = true;
  std::string message;
};

class ContentController {
 public:
  virtual ~ContentController() = default;
  virtual void BeginContent() = 0;
  [[nodiscard]] virtual ContentSnapshot Snapshot() const = 0;
  virtual void PauseContent() = 0;
  virtual void SeekContent(penumbra::MediaTime position) = 0;
  virtual void ResumeContent(const ContentSnapshot& snapshot) = 0;
  virtual void DispatchEnded() = 0;
};

class AdProvider {
 public:
  virtual ~AdProvider() = default;
  virtual ProviderResult Prepare(const AdBreak& ad_break) = 0;
  virtual ProviderResult Start(const Ad& ad) = 0;
  virtual ProviderProgress Advance(penumbra::MediaTime now) = 0;
};

struct AdManagerOptions {
  SeekPolicy seek_policy = SeekPolicy::kLast;
  bool replay_played_breaks = false;
  penumbra::TimeDelta provider_timeout = penumbra::TimeDelta::FromSeconds(30);
  penumbra::TimeDelta prefetch_lead =
      penumbra::TimeDelta::FromMilliseconds(5000);
  std::function<void(const AdBreak&)> on_prefetch;
  std::function<void(std::string_view, std::string_view, const AdBreak&)>
      on_tracking_event;
};

class AdManager {
 public:
  AdManager(AdManagerOptions options, ContentController& content,
            AdProvider& provider,
            std::function<void(const AdErrorEvent&)> on_ad_error);
  ~AdManager();
  AdManager(const AdManager&) = delete;
  AdManager& operator=(const AdManager&) = delete;

  [[nodiscard]] ScheduleResult SetBreaks(std::vector<AdBreak> breaks,
                                         penumbra::TimeDelta content_duration);
  void StartContent(penumbra::MediaTime now);
  void OnContentTime(penumbra::MediaTime previous, penumbra::MediaTime current,
                     penumbra::MediaTime now);
  void Seek(penumbra::MediaTime previous, penumbra::MediaTime target,
            penumbra::MediaTime now);
  void ContentEnded(penumbra::MediaTime now);
  void Tick(penumbra::MediaTime now);

  [[nodiscard]] bool InAdBreak() const;
  [[nodiscard]] bool EndedPending() const;
  [[nodiscard]] std::vector<penumbra::MediaTime> CuePoints() const;

 private:
  struct ScheduledBreak {
    AdBreak ad_break;
    penumbra::MediaTime time = penumbra::MediaTime::Unknown();
  };

  bool BeginBreak(std::size_t index, penumbra::MediaTime now);
  void FinishBreak();
  void FailBreak(ProviderStage stage, const ProviderResult& status);
  void FinishAd(penumbra::MediaTime now);
  void PlayQueued(penumbra::MediaTime now);
  void PrefetchDue(penumbra::MediaTime position);
  void DispatchTracking(std::string_view event, const AdBreak& ad_break) const;
  void QueueCrossed(penumbra::MediaTime previous, penumbra::MediaTime target);
  [[nodiscard]] bool IsPlayed(const ScheduledBreak& scheduled) const;

  AdManagerOptions options_;
  ContentController& content_;
  AdProvider& provider_;
  std::function<void(const AdErrorEvent&)> on_ad_error_;
  std::vector<ScheduledBreak> breaks_;
  std::vector<std::size_t> queued_;
  std::optional<std::size_t> active_break_;
  std::size_t active_ad_ = 0;
  std::optional<ContentSnapshot> snapshot_;
  penumbra::MediaTime stage_started_ = penumbra::MediaTime::Unknown();
  penumbra::MediaTime last_now_ = penumbra::MediaTime::Zero();
  penumbra::TimeDelta content_duration_ = penumbra::TimeDelta::Unknown();
  bool content_started_ = false;
  bool content_start_pending_ = false;
  bool content_ended_ = false;
  bool post_rolls_queued_ = false;
  bool ended_pending_ = false;
  std::vector<bool> prefetched_;
};

[[nodiscard]] std::optional<penumbra::MediaTime> ResolveOffset(
    const AdOffset& offset, penumbra::TimeDelta content_duration);

}

#endif
