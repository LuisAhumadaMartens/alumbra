#include "umbra/manifest/hls_refresh.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "penumbra/interface/media_time.h"
#include "umbra/manifest/ids.h"
#include "umbra/manifest/manifest_error.h"
#include "umbra/manifest/presentation.h"
#include "umbra/manifest/segment_index.h"

namespace umbra::manifest {
namespace {

using penumbra::MediaTime;
using penumbra::TimeDelta;

absl::Duration ToAbslDuration(TimeDelta duration) {
  return absl::Microseconds(duration.InMicroseconds());
}

const SegmentIndex* FindIndex(const Presentation& presentation,
                              const TrackId& track_id) {
  for (const std::shared_ptr<const Period>& period : presentation.periods) {
    if (period == nullptr) {
      continue;
    }
    for (const std::shared_ptr<const SwitchingSet>& set :
         period->switching_sets) {
      if (set == nullptr) {
        continue;
      }
      for (const std::shared_ptr<const Track>& track : set->tracks) {
        if (track != nullptr && track->id == track_id &&
            track->segments.is_resolved() &&
            track->segments.Index() != nullptr &&
            !track->segments.Index()->empty()) {
          return track->segments.Index();
        }
      }
    }
  }
  return nullptr;
}

}

HlsReloadDecision HlsPlaylistReloadPolicy::OnResponse(
    const Presentation& presentation, const TrackId& playlist_track,
    bool changed, absl::Time received_at, bool stale_playlist_failover) {
  HlsReloadDecision decision;
  if (presentation.protocol != Protocol::kHls ||
      presentation.type != PresentationType::kDynamic ||
      !presentation.live.has_value()) {
    last_change_.reset();
    return decision;
  }
  const LiveInfo& live = *presentation.live;
  if (live.target_segment_duration.has_value() &&
      live.target_segment_duration->is_finite() &&
      live.target_segment_duration->InMicroseconds() > 0) {
    const absl::Duration target = ToAbslDuration(*live.target_segment_duration);
    if (changed || !last_change_.has_value()) {
      last_change_ = received_at;
    }
    const SegmentIndex* index = FindIndex(presentation, playlist_track);
    absl::Duration delay = target / 2;
    if (changed && index != nullptr) {
      const TimeDelta last_duration = index->At(index->size() - 1).duration;
      if (last_duration.is_finite() && last_duration.InMicroseconds() > 0) {
        delay = ToAbslDuration(last_duration);
      }
    }
    decision.reload = true;
    decision.delay = delay;
    decision.failover = stale_playlist_failover && !changed &&
                        received_at - *last_change_ >= target * 3 / 2;
  }
  return decision;
}

std::vector<std::string> SelectedHlsPlaylistUris(
    const Presentation& presentation,
    const std::vector<TrackId>& selected_tracks) {
  const std::set<TrackId> selected(selected_tracks.begin(),
                                   selected_tracks.end());
  std::set<std::string> uris;
  for (const std::shared_ptr<const Period>& period : presentation.periods) {
    if (period == nullptr) {
      continue;
    }
    for (const std::shared_ptr<const SwitchingSet>& set :
         period->switching_sets) {
      if (set == nullptr) {
        continue;
      }
      for (const std::shared_ptr<const Track>& track : set->tracks) {
        if (track == nullptr || !selected.contains(track->id) ||
            !track->segments.Location().has_value()) {
          continue;
        }
        const std::string& uri = track->segments.Location()->uri;
        if (!uri.empty()) {
          uris.insert(uri);
        }
      }
    }
  }
  return {uris.begin(), uris.end()};
}

absl::StatusOr<MediaTime> HlsLiveStartPosition(const Presentation& presentation,
                                               std::int64_t start_offset_ms) {
  if (presentation.protocol != Protocol::kHls ||
      presentation.type != PresentationType::kDynamic ||
      !presentation.live.has_value()) {
    return InvalidManifest(
        "a live start position requires an HLS live presentation");
  }
  if (start_offset_ms < -1 || start_offset_ms > 3'600'000) {
    return InvalidManifest("the HLS live start offset is out of range");
  }
  const LiveInfo& live = *presentation.live;
  if (!live.window_start.is_finite() || !live.window_end.is_finite() ||
      live.window_end < live.window_start) {
    return InvalidManifest("the HLS live window is invalid");
  }
  TimeDelta distance = TimeDelta::Zero();
  if (start_offset_ms >= 0) {
    distance = TimeDelta::FromMilliseconds(start_offset_ms);
  } else if (presentation.hls.has_value() &&
             presentation.hls->start_offset.has_value()) {
    const TimeDelta offset = *presentation.hls->start_offset;
    MediaTime position = offset.InMicroseconds() >= 0
                             ? live.window_start + offset
                             : live.window_end + offset;
    position = std::clamp(position, live.window_start, live.window_end);
    return position;
  } else if (live.hold_back.has_value() && live.hold_back->is_finite() &&
             live.hold_back->InMicroseconds() > 0) {
    distance = *live.hold_back;
  } else if (live.target_segment_duration.has_value() &&
             live.target_segment_duration->is_finite() &&
             live.target_segment_duration->InMicroseconds() > 0) {
    distance = TimeDelta::FromMicroseconds(
        live.target_segment_duration->InMicroseconds() * 3);
  } else {
    return InvalidManifest(
        "the HLS live playlist has no start offset or target duration");
  }
  return std::max(live.window_start, live.window_end - distance);
}

}
