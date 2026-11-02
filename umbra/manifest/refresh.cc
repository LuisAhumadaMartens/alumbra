#include "umbra/manifest/refresh.h"

#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "umbra/manifest/ids.h"
#include "umbra/manifest/manifest_error.h"
#include "umbra/manifest/presentation.h"
#include "umbra/manifest/segment_index.h"
#include "umbra/manifest/validate.h"

namespace umbra::manifest {
namespace {

template <typename Id, typename T>
std::map<Id, const std::shared_ptr<const T>*> IndexById(
    const std::vector<std::shared_ptr<const T>>& items) {
  std::map<Id, const std::shared_ptr<const T>*> index;
  for (const std::shared_ptr<const T>& item : items) {
    if (item != nullptr) {
      index.emplace(item->id, &item);
    }
  }
  return index;
}

absl::StatusOr<std::shared_ptr<const Track>> ShareTrack(
    const std::shared_ptr<const Track>& previous,
    const std::shared_ptr<const Track>& next) {
  Track candidate = *next;
  const SegmentIndexProvider& before = previous->segments;
  if (next->segments.is_resolved()) {
    if (before.is_resolved()) {
      absl::StatusOr<std::shared_ptr<const SegmentIndex>> merged =
          MergeSegmentIndexes(before.SharedIndex(),
                              next->segments.SharedIndex());
      if (!merged.ok()) {
        return merged.status();
      }
      candidate.segments = next->segments.WithIndex(*std::move(merged));
    }
  } else if (before.is_resolved() &&
             before.Location() == next->segments.Location()) {
    candidate.segments = next->segments.WithIndex(before.SharedIndex());
  }
  if (candidate == *previous) {
    return previous;
  }
  return Freeze(std::move(candidate));
}

absl::StatusOr<std::vector<std::shared_ptr<const Track>>> ShareTracks(
    const std::vector<std::shared_ptr<const Track>>& previous,
    const std::vector<std::shared_ptr<const Track>>& next) {
  const auto before = IndexById<TrackId, Track>(previous);
  std::vector<std::shared_ptr<const Track>> shared;
  shared.reserve(next.size());
  for (const std::shared_ptr<const Track>& track : next) {
    if (track == nullptr) {
      shared.push_back(track);
      continue;
    }
    const auto found = before.find(track->id);
    if (found == before.end()) {
      shared.push_back(track);
      continue;
    }
    absl::StatusOr<std::shared_ptr<const Track>> result =
        ShareTrack(*found->second, track);
    if (!result.ok()) {
      return result.status();
    }
    shared.push_back(*std::move(result));
  }
  return shared;
}

absl::StatusOr<std::shared_ptr<const SwitchingSet>> ShareSet(
    const std::shared_ptr<const SwitchingSet>& previous,
    const std::shared_ptr<const SwitchingSet>& next) {
  absl::StatusOr<std::vector<std::shared_ptr<const Track>>> tracks =
      ShareTracks(previous->tracks, next->tracks);
  if (!tracks.ok()) {
    return tracks.status();
  }
  SwitchingSet candidate = *next;
  candidate.tracks = *std::move(tracks);
  if (candidate == *previous) {
    return previous;
  }
  return Freeze(std::move(candidate));
}

absl::StatusOr<std::shared_ptr<const Period>> SharePeriod(
    const std::shared_ptr<const Period>& previous,
    const std::shared_ptr<const Period>& next) {
  const auto before =
      IndexById<SwitchingSetId, SwitchingSet>(previous->switching_sets);
  Period candidate = *next;
  for (std::shared_ptr<const SwitchingSet>& set : candidate.switching_sets) {
    if (set == nullptr) {
      continue;
    }
    const auto found = before.find(set->id);
    if (found == before.end()) {
      continue;
    }
    absl::StatusOr<std::shared_ptr<const SwitchingSet>> result =
        ShareSet(*found->second, set);
    if (!result.ok()) {
      return result.status();
    }
    set = *std::move(result);
  }
  absl::StatusOr<std::vector<std::shared_ptr<const Track>>> trick_play =
      ShareTracks(previous->trick_play, next->trick_play);
  if (!trick_play.ok()) {
    return trick_play.status();
  }
  candidate.trick_play = *std::move(trick_play);
  if (candidate == *previous) {
    return previous;
  }
  return Freeze(std::move(candidate));
}

}

absl::StatusOr<std::shared_ptr<const Presentation>> RefreshPresentation(
    const std::shared_ptr<const Presentation>& previous, Presentation next) {
  if (previous == nullptr) {
    return BuildPresentation(std::move(next));
  }
  if (previous->protocol != next.protocol) {
    return InvalidManifest("a refresh changed the protocol");
  }
  if (previous->type == PresentationType::kStatic &&
      next.type == PresentationType::kDynamic) {
    return InvalidManifest("a finished presentation became live again");
  }
  const auto before = IndexById<PeriodId, Period>(previous->periods);
  for (std::shared_ptr<const Period>& period : next.periods) {
    if (period == nullptr) {
      continue;
    }
    const auto found = before.find(period->id);
    if (found == before.end()) {
      continue;
    }
    absl::StatusOr<std::shared_ptr<const Period>> result =
        SharePeriod(*found->second, period);
    if (!result.ok()) {
      return result.status();
    }
    period = *std::move(result);
  }
  if (SameContent(*previous, next)) {
    return previous;
  }
  next.generation = previous->generation + 1;
  return BuildPresentation(std::move(next));
}

}
