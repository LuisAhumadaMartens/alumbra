#include "umbra/fetch/recovery_ladder.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "umbra/base/time_ticks.h"
#include "umbra/fetch/backoff.h"
#include "umbra/fetch/content_steering.h"
#include "umbra/fetch/error_classifier.h"
#include "umbra/fetch/fetch_failure.h"
#include "umbra/fetch/fetch_types.h"
#include "umbra/fetch/recovery_errors.h"
#include "umbra/fetch/segment_fetcher.h"
#include "umbra/manifest/ids.h"
#include "umbra/manifest/segment_index.h"

namespace umbra::fetch {

namespace {

constexpr int kMaxRenditionAttempts = 64;

}

absl::Status ValidateRecoveryConfig(const RecoveryConfig& config) {
  if (absl::Status status = ValidateBackoffPolicy(config.backoff);
      !status.ok()) {
    return status;
  }
  if (config.rendition_exclusion < absl::ZeroDuration() ||
      config.pathway_exclusion < absl::ZeroDuration()) {
    return absl::InvalidArgumentError(
        "exclusion durations must not be negative");
  }
  return absl::OkStatus();
}

absl::StatusOr<std::shared_ptr<RecoveryLadder>> RecoveryLadder::Create(
    RecoveryDependencies dependencies, const RecoveryConfig& config) {
  if (absl::Status status = ValidateRecoveryConfig(config); !status.ok()) {
    return status;
  }
  if (dependencies.downstream == nullptr || dependencies.runner == nullptr ||
      dependencies.clock == nullptr || dependencies.redactor == nullptr ||
      dependencies.jitter == nullptr) {
    return absl::InvalidArgumentError(
        "downstream, runner, clock, redactor and jitter are required");
  }
  return std::shared_ptr<RecoveryLadder>(
      new RecoveryLadder(std::move(dependencies), config));
}

RecoveryLadder::RecoveryLadder(RecoveryDependencies dependencies,
                               const RecoveryConfig& config)
    : deps_(std::move(dependencies)), config_(config) {}

RecoveryLadder::~RecoveryLadder() = default;

void RecoveryLadder::SetTarget(RecoveryTarget* target) { target_ = target; }

base::TimeTicks RecoveryLadder::Now() const { return deps_.clock->NowTicks(); }

std::string RecoveryLadder::OriginOf(const std::string& url) {
  const size_t scheme_end = url.find("://");
  const size_t start = scheme_end == std::string::npos ? 0 : scheme_end + 3;
  const size_t end = url.find_first_of("/?#", start);
  return url.substr(0, end);
}

std::string RecoveryLadder::PairKey(const std::string& pathway,
                                    const manifest::TrackId& track) {
  return absl::StrCat(pathway, "\n", track.Value());
}

StreamSelection RecoveryLadder::SelectionFor(const StreamState& state,
                                             const RedundantSource& source) {
  StreamSelection selection;
  selection.track = source.track;
  selection.base_url = source.base_url;
  selection.dynamic = state.dynamic;
  return selection;
}

std::vector<RedundantSource> RecoveryLadder::OrderedSources(
    const manifest::TrackId& track) {
  std::vector<RedundantSource> sources;
  if (config_.failover_enabled && deps_.redundancy != nullptr) {
    sources = deps_.redundancy->SourcesFor(track);
  }
  if (config_.content_steering_enabled && !steering_order_.empty()) {
    const size_t unlisted = steering_order_.size();
    const auto rank = [this, unlisted](const RedundantSource& source) {
      const auto found = std::ranges::find(steering_order_, source.pathway_id);
      return found == steering_order_.end()
                 ? unlisted
                 : static_cast<size_t>(found - steering_order_.begin());
    };
    std::ranges::stable_sort(
        sources, [&rank](const RedundantSource& a, const RedundantSource& b) {
          return rank(a) < rank(b);
        });
  }
  return sources;
}

bool RecoveryLadder::SourceExcluded(const StreamState& state,
                                    const RedundantSource& source) const {
  const base::TimeTicks now = Now();
  return pathways_.IsExcluded(source.pathway_id, now) ||
         pathways_.IsExcluded(PairKey(source.pathway_id, state.rendition), now);
}

std::optional<size_t> RecoveryLadder::BestSource(
    const StreamState& state, const std::optional<size_t>& skip) const {
  for (size_t i = 0; i < state.sources.size(); ++i) {
    if (skip.has_value() && *skip == i) {
      continue;
    }
    if (!SourceExcluded(state, state.sources[i])) {
      return i;
    }
  }
  return std::nullopt;
}

bool RecoveryLadder::Install(StreamKind kind, const StreamSelection& selection,
                             SwitchMode mode, bool require_usable) {
  StreamState& state = StateOf(kind);
  ++state.epoch;
  state.attempts = 0;
  state.active = selection.track != nullptr;
  state.dynamic = selection.dynamic;
  state.requested = selection;
  if (!state.active) {
    state.sources.clear();
    return false;
  }
  state.rendition = selection.track->id;
  state.sources = OrderedSources(state.rendition);
  if (state.sources.empty()) {
    RedundantSource only;
    only.pathway_id = OriginOf(selection.base_url);
    only.track = selection.track;
    only.base_url = selection.base_url;
    state.sources.push_back(std::move(only));
  }
  const std::optional<size_t> best = BestSource(state, std::nullopt);
  if (!best.has_value() && require_usable) {
    return false;
  }
  state.current = best.value_or(0);
  return Apply(kind, mode, state.current);
}

bool RecoveryLadder::Apply(StreamKind kind, SwitchMode mode, size_t source) {
  StreamState& state = StateOf(kind);
  if (target_ == nullptr) {
    return false;
  }
  const absl::Status status = target_->SelectTrack(
      kind, SelectionFor(state, state.sources.at(source)), mode);
  return status.ok();
}

absl::Status RecoveryLadder::SelectTrack(StreamKind kind,
                                         const StreamSelection& selection,
                                         SwitchMode mode) {
  if (target_ == nullptr) {
    return absl::FailedPreconditionError("the recovery target is not set");
  }
  if (selection.track == nullptr) {
    return absl::InvalidArgumentError("a track is required");
  }
  if (!Install(kind, selection, mode, false)) {
    return absl::InvalidArgumentError("the selection could not be applied");
  }
  return absl::OkStatus();
}

void RecoveryLadder::ClearTrack(StreamKind kind) {
  StreamState& state = StateOf(kind);
  ++state.epoch;
  state.active = false;
  state.sources.clear();
  state.attempts = 0;
  if (target_ != nullptr) {
    target_->ClearTrack(kind);
  }
}

void RecoveryLadder::OnSeek() {
  for (StreamState& state : streams_) {
    ++state.epoch;
    state.attempts = 0;
  }
}

void RecoveryLadder::OnInitSegment(InitSegmentData init) {
  if (!init.from_cache) {
    StateOf(init.kind).attempts = 0;
  }
  deps_.downstream->OnInitSegment(std::move(init));
}

void RecoveryLadder::OnMediaSegment(MediaSegmentData media) {
  StateOf(media.kind).attempts = 0;
  deps_.downstream->OnMediaSegment(std::move(media));
}

void RecoveryLadder::OnEndOfStream(StreamKind kind, uint64_t generation) {
  deps_.downstream->OnEndOfStream(kind, generation);
}

void RecoveryLadder::OnIndexResolved(
    StreamKind kind, const manifest::TrackId& track,
    const manifest::SegmentIndexProvider& provider) {
  StateOf(kind).attempts = 0;
  deps_.downstream->OnIndexResolved(kind, track, provider);
}

void RecoveryLadder::OnStreamError(StreamKind kind, uint64_t generation,
                                   absl::Status status) {
  const uint64_t epoch = StateOf(kind).epoch;
  deps_.runner->PostTask([weak = weak_from_this(), kind, epoch, generation,
                          status = std::move(status)]() mutable {
    if (const std::shared_ptr<RecoveryLadder> self = weak.lock()) {
      self->Handle(kind, epoch, generation, std::move(status));
    }
  });
}

void RecoveryLadder::Emit(RecoveryStep step, StreamKind kind,
                          const std::string& pathway, int attempt,
                          absl::Duration delay, const absl::Status& cause) {
  if (deps_.observer == nullptr) {
    return;
  }
  RecoveryEvent event;
  event.step = step;
  event.kind = kind;
  event.track = StateOf(kind).rendition;
  event.pathway_id = pathway;
  event.attempt = attempt;
  event.delay = delay;
  event.cause = cause;
  deps_.observer->OnRecoveryEvent(event);
}

std::string RecoveryLadder::CurrentPathway(StreamKind kind) const {
  const StreamState& state = StateOf(kind);
  if (!state.active || state.current >= state.sources.size()) {
    return "";
  }
  return state.sources[state.current].pathway_id;
}

int RecoveryLadder::Attempts(StreamKind kind) const {
  return StateOf(kind).attempts;
}

bool RecoveryLadder::IsPathwayExcluded(const std::string& pathway_id) const {
  return pathways_.IsExcluded(pathway_id, Now());
}

void RecoveryLadder::Handle(StreamKind kind, uint64_t epoch,
                            uint64_t generation, absl::Status status) {
  StreamState& state = StateOf(kind);
  if (!state.active || epoch != state.epoch || target_ == nullptr) {
    if (state.epoch == epoch) {
      deps_.downstream->OnStreamError(kind, generation, std::move(status));
    }
    return;
  }
  const std::optional<FetchFailure> failure = GetFetchFailure(status);
  LivePosition live = LivePosition::kNotLive;
  if (deps_.live != nullptr && failure.has_value()) {
    live = deps_.live->Locate(kind, *failure);
  }
  const Classification classification =
      ClassifyFailure(status, failure, live, config_.backoff);
  const std::string pathway = CurrentPathway(kind);
  switch (classification.action) {
    case RecoveryAction::kFatal:
      Fatal(kind, generation, status, failure);
      return;
    case RecoveryAction::kResync:
      if (state.attempts < config_.backoff.max_attempts) {
        ++state.attempts;
        Emit(RecoveryStep::kResync, kind, pathway, state.attempts,
             absl::ZeroDuration(), status);
        target_->Resync(kind);
        return;
      }
      break;
    case RecoveryAction::kRetry:
      if (state.attempts < config_.backoff.max_attempts) {
        const absl::Duration delay =
            classification.retry_delay.has_value()
                ? *classification.retry_delay
                : BackoffDelay(config_.backoff, state.attempts, *deps_.jitter);
        ++state.attempts;
        Emit(RecoveryStep::kRetry, kind, pathway, state.attempts, delay,
             status);
        ScheduleRetry(kind, epoch, delay);
        return;
      }
      break;
    case RecoveryAction::kFailover:
      if (classification.pathway_hold.has_value() && !pathway.empty()) {
        pathways_.HoldUntil(pathway, Now() + *classification.pathway_hold);
      }
      break;
  }
  Escalate(kind, generation, status, failure, classification);
}

void RecoveryLadder::ScheduleRetry(StreamKind kind, uint64_t epoch,
                                   absl::Duration delay) {
  deps_.runner->PostDelayedTask(
      [weak = weak_from_this(), kind, epoch] {
        const std::shared_ptr<RecoveryLadder> self = weak.lock();
        if (self == nullptr || self->target_ == nullptr ||
            self->StateOf(kind).epoch != epoch) {
          return;
        }
        self->target_->Retry(kind);
      },
      delay);
}

void RecoveryLadder::ExcludeSource(StreamKind kind,
                                   const Classification& classification) {
  const StreamState& state = StateOf(kind);
  if (state.current >= state.sources.size()) {
    return;
  }
  const std::string& pathway = state.sources[state.current].pathway_id;
  const std::string key =
      classification.pathway_wide ? pathway : PairKey(pathway, state.rendition);
  const bool steered =
      config_.content_steering_enabled && !steering_order_.empty();
  pathways_.Exclude(key, Now(), config_.pathway_exclusion, steered);
}

void RecoveryLadder::Escalate(StreamKind kind, uint64_t generation,
                              const absl::Status& status,
                              const std::optional<FetchFailure>& failure,
                              const Classification& classification) {
  if (config_.failover_enabled) {
    ExcludeSource(kind, classification);
    if (FailOver(kind, status)) {
      return;
    }
    if (classification.allows_rendition_step &&
        ExcludeRendition(kind, status)) {
      return;
    }
  }
  Fatal(kind, generation, status, failure);
}

bool RecoveryLadder::FailOver(StreamKind kind, const absl::Status& status) {
  StreamState& state = StateOf(kind);
  const std::optional<size_t> next = BestSource(state, state.current);
  if (!next.has_value()) {
    return false;
  }
  const size_t previous = state.current;
  state.current = *next;
  state.attempts = 0;
  ++state.epoch;
  if (!Apply(kind, SwitchMode::kCancelInFlight, state.current)) {
    state.current = previous;
    return false;
  }
  Emit(RecoveryStep::kFailover, kind, state.sources[state.current].pathway_id,
       0, absl::ZeroDuration(), status);
  return true;
}

bool RecoveryLadder::ExcludeRendition(StreamKind kind,
                                      const absl::Status& status) {
  if (deps_.renditions == nullptr) {
    return false;
  }
  for (int i = 0; i < kMaxRenditionAttempts; ++i) {
    const manifest::TrackId failed = StateOf(kind).rendition;
    deps_.renditions->ExcludeRendition(kind, failed,
                                       Now() + config_.rendition_exclusion);
    Emit(RecoveryStep::kRenditionExcluded, kind, CurrentPathway(kind), 0,
         config_.rendition_exclusion, status);
    deps_.runner->PostDelayedTask(
        [weak = weak_from_this(), kind, failed] {
          if (const std::shared_ptr<RecoveryLadder> self = weak.lock()) {
            self->deps_.renditions->RestoreRendition(kind, failed);
            self->Emit(RecoveryStep::kRenditionRestored, kind, "", 0,
                       absl::ZeroDuration(), absl::OkStatus());
          }
        },
        config_.rendition_exclusion);
    std::optional<StreamSelection> alternative =
        deps_.renditions->SelectAlternative(kind, failed);
    if (!alternative.has_value() || alternative->track == nullptr) {
      return false;
    }
    if (Install(kind, *alternative, SwitchMode::kCancelInFlight, true)) {
      return true;
    }
  }
  return false;
}

void RecoveryLadder::Fatal(StreamKind kind, uint64_t generation,
                           const absl::Status& status,
                           const std::optional<FetchFailure>& failure) {
  const StreamState& state = StateOf(kind);
  absl::Status error = MakeRecoveryExhaustedError(*deps_.redactor, status,
                                                  failure, state.attempts);
  Emit(RecoveryStep::kFatal, kind, CurrentPathway(kind), state.attempts,
       absl::ZeroDuration(), status);
  deps_.downstream->OnStreamError(kind, generation, std::move(error));
}

bool RecoveryLadder::OnPlaylistFailover(StreamKind kind) {
  StreamState& state = StateOf(kind);
  if (!config_.failover_enabled || !state.active || target_ == nullptr) {
    return false;
  }
  const std::optional<size_t> next = BestSource(state, state.current);
  if (!next.has_value()) {
    return false;
  }
  Classification classification;
  classification.pathway_wide = false;
  ExcludeSource(kind, classification);
  const size_t previous = state.current;
  state.current = *next;
  state.attempts = 0;
  ++state.epoch;
  if (!Apply(kind, SwitchMode::kAfterInFlight, state.current)) {
    state.current = previous;
    return false;
  }
  Emit(RecoveryStep::kPlaylistFailover, kind,
       state.sources[state.current].pathway_id, 0, absl::ZeroDuration(),
       absl::OkStatus());
  return true;
}

void RecoveryLadder::OnSteeringManifest(const SteeringManifest& manifest) {
  if (!config_.content_steering_enabled) {
    return;
  }
  steering_order_ = manifest.pathway_priority;
  pathways_.OnSteeringRefresh();
  if (deps_.redundancy != nullptr) {
    deps_.redundancy->OnPathwayClones(manifest.pathway_clones);
  }
  for (const StreamKind kind : {StreamKind::kVideo, StreamKind::kAudio}) {
    StreamState& state = StateOf(kind);
    if (!state.active || state.current >= state.sources.size()) {
      continue;
    }
    const RedundantSource current = state.sources[state.current];
    state.sources = OrderedSources(state.rendition);
    if (state.sources.empty()) {
      state.sources.push_back(current);
    }
    const std::optional<size_t> best = BestSource(state, std::nullopt);
    state.current = 0;
    for (size_t i = 0; i < state.sources.size(); ++i) {
      if (state.sources[i].pathway_id == current.pathway_id &&
          state.sources[i].base_url == current.base_url) {
        state.current = i;
        break;
      }
    }
    if (best.has_value() && *best != state.current) {
      const size_t previous = state.current;
      state.current = *best;
      ++state.epoch;
      state.attempts = 0;
      if (!Apply(kind, SwitchMode::kAfterInFlight, state.current)) {
        state.current = previous;
        continue;
      }
    }
    Emit(RecoveryStep::kSteeringApplied, kind,
         state.sources[state.current].pathway_id, 0, manifest.ttl,
         absl::OkStatus());
  }
}

void RecoveryLadder::OnSteeringFailure(
    [[maybe_unused]] const absl::Status& status) {}

}
