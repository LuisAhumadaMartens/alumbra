#include "umbra/manifest/presentation.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "penumbra/interface/media_time.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/codec/codec_string.h"
#include "umbra/manifest/ids.h"
#include "umbra/manifest/segment_index.h"

namespace umbra::manifest {
namespace {

using penumbra::MediaTime;

template <typename T>
bool SameChildren(const std::vector<std::shared_ptr<const T>>& a,
                  const std::vector<std::shared_ptr<const T>>& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i] != b[i] &&
        (a[i] == nullptr || b[i] == nullptr || !(*a[i] == *b[i]))) {
      return false;
    }
  }
  return true;
}

bool SameIndexTimeline(const SegmentIndexProvider& a,
                       const SegmentIndexProvider& b) {
  const SegmentIndex* left = a.Index();
  const SegmentIndex* right = b.Index();
  if (left == nullptr || right == nullptr) {
    return left == right;
  }
  if (left->size() != right->size()) {
    return false;
  }
  for (std::size_t i = 0; i < left->size(); ++i) {
    if (left->StartOf(i) != right->StartOf(i) ||
        left->EndOf(i) != right->EndOf(i)) {
      return false;
    }
  }
  return true;
}

bool EquivalentTracks(const Track& a, const Track& b) {
  return a.bandwidth_bps == b.bandwidth_bps && a.codecs == b.codecs &&
         a.format == b.format && SameIndexTimeline(a.segments, b.segments);
}

auto TrackKey(const Track& track) {
  std::int32_t width = 0;
  std::int32_t height = 0;
  if (const auto* video = std::get_if<VideoTrackFormat>(&track.format)) {
    width = video->width;
    height = video->height;
  }
  return std::make_tuple(track.bandwidth_bps, width, height);
}

template <typename T, typename Key>
std::vector<std::size_t> SortedOrder(
    const std::vector<std::shared_ptr<const T>>& items, Key key) {
  std::vector<std::size_t> order;
  order.reserve(items.size());
  for (std::size_t i = 0; i < items.size(); ++i) {
    order.push_back(i);
  }
  std::ranges::stable_sort(order, [&](std::size_t x, std::size_t y) {
    return key(*items[x]) < key(*items[y]);
  });
  return order;
}

template <typename T, typename Key, typename Same>
bool EquivalentLists(const std::vector<std::shared_ptr<const T>>& a,
                     const std::vector<std::shared_ptr<const T>>& b, Key key,
                     Same same) {
  if (a.size() != b.size()) {
    return false;
  }
  const std::vector<std::size_t> left = SortedOrder(a, key);
  const std::vector<std::size_t> right = SortedOrder(b, key);
  for (std::size_t i = 0; i < left.size(); ++i) {
    if (!same(*a[left[i]], *b[right[i]])) {
      return false;
    }
  }
  return true;
}

bool EquivalentTrackLists(const std::vector<std::shared_ptr<const Track>>& a,
                          const std::vector<std::shared_ptr<const Track>>& b) {
  return EquivalentLists(a, b, TrackKey, EquivalentTracks);
}

auto SetKey(const SwitchingSet& set) {
  return std::make_tuple(set.type, set.language, set.roles,
                         set.channels.channel_count, set.tracks.size());
}

bool EquivalentSets(const SwitchingSet& a, const SwitchingSet& b) {
  const bool same_protection =
      a.protection.has_value() == b.protection.has_value() &&
      (!a.protection.has_value() ||
       (a.protection->scheme == b.protection->scheme &&
        a.protection->system_ids == b.protection->system_ids));
  return SetKey(a) == SetKey(b) && a.channels == b.channels &&
         same_protection && EquivalentTrackLists(a.tracks, b.tracks);
}

bool EquivalentPeriods(const Period& a, const Period& b) {
  return a.start == b.start && a.duration == b.duration &&
         EquivalentTrackLists(a.trick_play, b.trick_play) &&
         EquivalentLists(a.switching_sets, b.switching_sets, SetKey,
                         EquivalentSets);
}

}

penumbra::StreamType Track::type() const {
  if (std::holds_alternative<AudioTrackFormat>(format)) {
    return penumbra::StreamType::kAudio;
  }
  if (std::holds_alternative<TextTrackFormat>(format)) {
    return penumbra::StreamType::kText;
  }
  return penumbra::StreamType::kVideo;
}

std::vector<penumbra::CodecId> SwitchingSet::CodecIds() const {
  std::vector<penumbra::CodecId> ids;
  for (const std::shared_ptr<const Track>& track : tracks) {
    for (const codec::CodecDescription& description : track->codecs) {
      const penumbra::CodecId id = codec::CodecIdOf(description);
      if (std::ranges::find(ids, id) == ids.end()) {
        ids.push_back(id);
      }
    }
  }
  return ids;
}

const Track* SwitchingSet::FindTrack(const TrackId& track_id) const {
  for (const std::shared_ptr<const Track>& track : tracks) {
    if (track->id == track_id) {
      return track.get();
    }
  }
  return nullptr;
}

MediaTime Period::End() const {
  if (!duration.is_finite()) {
    return MediaTime::Infinite();
  }
  return start + duration;
}

const SwitchingSet* Period::FindSwitchingSet(
    const SwitchingSetId& set_id) const {
  for (const std::shared_ptr<const SwitchingSet>& set : switching_sets) {
    if (set->id == set_id) {
      return set.get();
    }
  }
  return nullptr;
}

const Track* Period::FindTrack(const TrackId& track_id) const {
  for (const std::shared_ptr<const SwitchingSet>& set : switching_sets) {
    if (const Track* track = set->FindTrack(track_id)) {
      return track;
    }
  }
  for (const std::shared_ptr<const Track>& track : trick_play) {
    if (track->id == track_id) {
      return track.get();
    }
  }
  return nullptr;
}

const SwitchingSet* Period::FindSwitchingSetOf(const TrackId& track_id) const {
  for (const std::shared_ptr<const SwitchingSet>& set : switching_sets) {
    if (set->FindTrack(track_id) != nullptr) {
      return set.get();
    }
  }
  return nullptr;
}

const Period* Presentation::FindPeriod(MediaTime time) const {
  if (!time.is_finite()) {
    return nullptr;
  }
  const auto it =
      std::ranges::upper_bound(periods, time, std::ranges::less{},
                               [](const std::shared_ptr<const Period>& period) {
                                 return period->start;
                               });
  if (it == periods.begin()) {
    return nullptr;
  }
  const Period* candidate = std::prev(it)->get();
  return time < candidate->End() ? candidate : nullptr;
}

const Period* Presentation::FindPeriodById(const PeriodId& period_id) const {
  for (const std::shared_ptr<const Period>& period : periods) {
    if (period->id == period_id) {
      return period.get();
    }
  }
  return nullptr;
}

bool operator==(const SwitchingSet& a, const SwitchingSet& b) {
  return a.id == b.id && a.type == b.type && a.language == b.language &&
         a.roles == b.roles && a.label == b.label && a.channels == b.channels &&
         a.is_default == b.is_default && a.autoselect == b.autoselect &&
         a.forced == b.forced && a.protection == b.protection &&
         SameChildren(a.tracks, b.tracks);
}

bool operator==(const Period& a, const Period& b) {
  return a.id == b.id && a.start == b.start && a.duration == b.duration &&
         SameChildren(a.switching_sets, b.switching_sets) &&
         SameChildren(a.trick_play, b.trick_play);
}

bool SameContent(const Presentation& a, const Presentation& b) {
  return a.protocol == b.protocol && a.type == b.type &&
         a.duration == b.duration && a.live == b.live &&
         a.ad_breaks == b.ad_breaks && SameChildren(a.periods, b.periods);
}

std::shared_ptr<const Track> Freeze(Track track) {
  return std::make_shared<const Track>(std::move(track));
}

std::shared_ptr<const SwitchingSet> Freeze(SwitchingSet set) {
  return std::make_shared<const SwitchingSet>(std::move(set));
}

std::shared_ptr<const Period> Freeze(Period period) {
  return std::make_shared<const Period>(std::move(period));
}

bool Equivalent(const Presentation& a, const Presentation& b) {
  if (a.type != b.type || a.duration != b.duration ||
      a.periods.size() != b.periods.size() ||
      a.ad_breaks.size() != b.ad_breaks.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.ad_breaks.size(); ++i) {
    if (a.ad_breaks[i].start != b.ad_breaks[i].start ||
        a.ad_breaks[i].duration != b.ad_breaks[i].duration) {
      return false;
    }
  }
  for (std::size_t i = 0; i < a.periods.size(); ++i) {
    if (!EquivalentPeriods(*a.periods[i], *b.periods[i])) {
      return false;
    }
  }
  return true;
}

}
