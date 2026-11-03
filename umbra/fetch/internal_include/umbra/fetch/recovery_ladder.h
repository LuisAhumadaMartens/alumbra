#ifndef UMBRA_FETCH_RECOVERY_LADDER_H_
#define UMBRA_FETCH_RECOVERY_LADDER_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/tick_clock.h"
#include "umbra/base/time_ticks.h"
#include "umbra/base/url_redactor.h"
#include "umbra/fetch/backoff.h"
#include "umbra/fetch/content_steering.h"
#include "umbra/fetch/error_classifier.h"
#include "umbra/fetch/fetch_failure.h"
#include "umbra/fetch/fetch_types.h"
#include "umbra/fetch/pathway_table.h"
#include "umbra/fetch/segment_fetcher.h"
#include "umbra/manifest/ids.h"
#include "umbra/manifest/presentation.h"

namespace umbra::fetch {

struct RecoveryConfig {
  bool failover_enabled = true;
  bool content_steering_enabled = true;
  BackoffPolicy backoff;
  absl::Duration rendition_exclusion = absl::Seconds(60);
  absl::Duration pathway_exclusion = absl::Seconds(300);
};

[[nodiscard]] absl::Status ValidateRecoveryConfig(const RecoveryConfig& config);

struct RedundantSource {
  std::string pathway_id;
  std::shared_ptr<const manifest::Track> track;
  std::string base_url;
};

class RedundancyProvider {
 public:
  RedundancyProvider() = default;
  RedundancyProvider(const RedundancyProvider&) = delete;
  RedundancyProvider& operator=(const RedundancyProvider&) = delete;
  virtual ~RedundancyProvider() = default;

  [[nodiscard]] virtual std::vector<RedundantSource> SourcesFor(
      const manifest::TrackId& track) = 0;
  virtual void OnPathwayClones(const std::vector<PathwayClone>& clones) = 0;
};

class RenditionControl {
 public:
  RenditionControl() = default;
  RenditionControl(const RenditionControl&) = delete;
  RenditionControl& operator=(const RenditionControl&) = delete;
  virtual ~RenditionControl() = default;

  virtual void ExcludeRendition(StreamKind kind, const manifest::TrackId& track,
                                base::TimeTicks until) = 0;
  virtual void RestoreRendition(StreamKind kind,
                                const manifest::TrackId& track) = 0;
  [[nodiscard]] virtual std::optional<StreamSelection> SelectAlternative(
      StreamKind kind, const manifest::TrackId& failed) = 0;
};

class LivePositionProbe {
 public:
  LivePositionProbe() = default;
  LivePositionProbe(const LivePositionProbe&) = delete;
  LivePositionProbe& operator=(const LivePositionProbe&) = delete;
  virtual ~LivePositionProbe() = default;

  [[nodiscard]] virtual LivePosition Locate(StreamKind kind,
                                            const FetchFailure& failure) = 0;
};

enum class RecoveryStep : uint8_t {
  kRetry,
  kResync,
  kFailover,
  kRenditionExcluded,
  kRenditionRestored,
  kPlaylistFailover,
  kSteeringApplied,
  kFatal,
};

struct RecoveryEvent {
  RecoveryStep step = RecoveryStep::kRetry;
  StreamKind kind = StreamKind::kVideo;
  manifest::TrackId track;
  std::string pathway_id;
  int attempt = 0;
  absl::Duration delay;
  absl::Status cause;
};

class RecoveryObserver {
 public:
  RecoveryObserver() = default;
  RecoveryObserver(const RecoveryObserver&) = delete;
  RecoveryObserver& operator=(const RecoveryObserver&) = delete;
  virtual ~RecoveryObserver() = default;

  virtual void OnRecoveryEvent(const RecoveryEvent& event) = 0;
};

class RecoveryTarget {
 public:
  RecoveryTarget() = default;
  RecoveryTarget(const RecoveryTarget&) = delete;
  RecoveryTarget& operator=(const RecoveryTarget&) = delete;
  virtual ~RecoveryTarget() = default;

  virtual absl::Status SelectTrack(StreamKind kind, StreamSelection selection,
                                   SwitchMode mode) = 0;
  virtual void Retry(StreamKind kind) = 0;
  virtual void Resync(StreamKind kind) = 0;
  virtual void ClearTrack(StreamKind kind) = 0;
};

class FetcherRecoveryTarget final : public RecoveryTarget {
 public:
  explicit FetcherRecoveryTarget(SegmentFetcher* fetcher) : fetcher_(fetcher) {}

  absl::Status SelectTrack(StreamKind kind, StreamSelection selection,
                           SwitchMode mode) override {
    absl::Status status =
        fetcher_->SelectTrack(kind, std::move(selection), mode);
    if (status.ok()) {
      fetcher_->Retry(kind);
    }
    return status;
  }
  void Retry(StreamKind kind) override { fetcher_->Retry(kind); }
  void Resync(StreamKind kind) override { fetcher_->Retry(kind); }
  void ClearTrack(StreamKind kind) override { fetcher_->ClearTrack(kind); }

 private:
  SegmentFetcher* fetcher_;
};

struct RecoveryDependencies {
  std::shared_ptr<SegmentSink> downstream;
  std::shared_ptr<base::SequencedTaskRunner> runner;
  const base::TickClock* clock = nullptr;
  std::shared_ptr<const base::UrlRedactor> redactor;
  std::shared_ptr<JitterSource> jitter;
  std::shared_ptr<RedundancyProvider> redundancy;
  std::shared_ptr<RenditionControl> renditions;
  std::shared_ptr<RecoveryObserver> observer;
  std::shared_ptr<LivePositionProbe> live;
};

class RecoveryLadder final
    : public SegmentSink,
      public SteeringListener,
      public std::enable_shared_from_this<RecoveryLadder> {
 public:
  static absl::StatusOr<std::shared_ptr<RecoveryLadder>> Create(
      RecoveryDependencies dependencies, const RecoveryConfig& config);

  ~RecoveryLadder() override;
  RecoveryLadder(const RecoveryLadder&) = delete;
  RecoveryLadder& operator=(const RecoveryLadder&) = delete;

  void SetTarget(RecoveryTarget* target);

  absl::Status SelectTrack(StreamKind kind, const StreamSelection& selection,
                           SwitchMode mode);
  void ClearTrack(StreamKind kind);
  void OnSeek();
  bool OnPlaylistFailover(StreamKind kind);

  void OnInitSegment(InitSegmentData init) override;
  void OnMediaSegment(MediaSegmentData media) override;
  void OnEndOfStream(StreamKind kind, uint64_t generation) override;
  void OnStreamError(StreamKind kind, uint64_t generation,
                     absl::Status status) override;
  void OnIndexResolved(StreamKind kind, const manifest::TrackId& track,
                       const manifest::SegmentIndexProvider& provider) override;

  void OnSteeringManifest(const SteeringManifest& manifest) override;
  void OnSteeringFailure(const absl::Status& status) override;

  [[nodiscard]] bool IsPathwayExcluded(const std::string& pathway_id) const;
  [[nodiscard]] std::string CurrentPathway(StreamKind kind) const;
  [[nodiscard]] int Attempts(StreamKind kind) const;

 private:
  struct StreamState {
    bool active = false;
    uint64_t epoch = 0;
    StreamSelection requested;
    manifest::TrackId rendition;
    std::vector<RedundantSource> sources;
    size_t current = 0;
    int attempts = 0;
    bool dynamic = false;
  };

  RecoveryLadder(RecoveryDependencies dependencies,
                 const RecoveryConfig& config);

  void Handle(StreamKind kind, uint64_t epoch, uint64_t generation,
              absl::Status status);
  void ScheduleRetry(StreamKind kind, uint64_t epoch, absl::Duration delay);
  void Escalate(StreamKind kind, uint64_t generation,
                const absl::Status& status,
                const std::optional<FetchFailure>& failure,
                const Classification& classification);
  bool FailOver(StreamKind kind, const absl::Status& status);
  bool ExcludeRendition(StreamKind kind, const absl::Status& status);
  void Fatal(StreamKind kind, uint64_t generation, const absl::Status& status,
             const std::optional<FetchFailure>& failure);
  void ExcludeSource(StreamKind kind, const Classification& classification);
  void Emit(RecoveryStep step, StreamKind kind, const std::string& pathway,
            int attempt, absl::Duration delay, const absl::Status& cause);
  bool Apply(StreamKind kind, SwitchMode mode, size_t source);
  bool Install(StreamKind kind, const StreamSelection& selection,
               SwitchMode mode, bool require_usable);

  [[nodiscard]] std::vector<RedundantSource> OrderedSources(
      const manifest::TrackId& track);
  [[nodiscard]] std::optional<size_t> BestSource(
      const StreamState& state, const std::optional<size_t>& skip) const;
  [[nodiscard]] bool SourceExcluded(const StreamState& state,
                                    const RedundantSource& source) const;
  [[nodiscard]] static std::string PairKey(const std::string& pathway,
                                           const manifest::TrackId& track);
  [[nodiscard]] static std::string OriginOf(const std::string& url);
  [[nodiscard]] static StreamSelection SelectionFor(
      const StreamState& state, const RedundantSource& source);
  [[nodiscard]] base::TimeTicks Now() const;
  StreamState& StateOf(StreamKind kind) {
    return streams_.at(static_cast<size_t>(kind));
  }
  [[nodiscard]] const StreamState& StateOf(StreamKind kind) const {
    return streams_.at(static_cast<size_t>(kind));
  }

  RecoveryDependencies deps_;
  RecoveryConfig config_;
  RecoveryTarget* target_ = nullptr;
  PathwayTable pathways_;
  std::vector<std::string> steering_order_;
  std::array<StreamState, kStreamKindCount> streams_;
};

}

#endif
