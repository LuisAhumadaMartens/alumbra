#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/rational.h"
#include "umbra/base/status.h"
#include "umbra/codec/reader_util.h"
#include "umbra/demux/mp4_tables.h"
#include "umbra/demux/mp4_types.h"

namespace umbra::demux {
namespace {

constexpr int64_t kMaxTimelineMagnitude = int64_t{1} << 61;

absl::Status AssignDurations(const RawSampleTable& table, SampleList& list) {
  size_t index = 0;
  int64_t dts = 0;
  for (const SttsEntry& entry : table.stts) {
    for (uint32_t count = 0; count < entry.count && index < list.size();
         ++count) {
      list[index].dts = dts;
      list[index].duration = entry.delta;
      dts += entry.delta;
      ++index;
    }
  }
  if (index < list.size()) {
    return codec::Corrupt("stts covers fewer samples than stsz");
  }
  return absl::OkStatus();
}

void AssignCompositionOffsets(const RawSampleTable& table, SampleList& list) {
  size_t index = 0;
  for (const CttsEntry& entry : table.ctts) {
    for (uint32_t count = 0; count < entry.count && index < list.size();
         ++count) {
      list[index].cts_offset = entry.offset;
      ++index;
    }
  }
}

void AssignSizes(const RawSampleTable& table, SampleList& list) {
  for (size_t index = 0; index < list.size(); ++index) {
    list[index].size =
        table.sizes.empty() ? table.constant_size : table.sizes[index];
  }
}

absl::Status ValidateChunkMap(const RawSampleTable& table) {
  if (table.stsc.empty() || table.chunk_offsets.empty()) {
    return codec::Corrupt("sample table has no chunk map");
  }
  uint32_t previous = 0;
  for (const StscEntry& entry : table.stsc) {
    if (entry.first_chunk == 0 || entry.first_chunk < previous) {
      return codec::Corrupt("stsc first_chunk is not ascending");
    }
    previous = entry.first_chunk;
  }
  return absl::OkStatus();
}

absl::Status AssignOffsets(const RawSampleTable& table, SampleList& list) {
  ALUMBRA_RETURN_IF_ERROR(ValidateChunkMap(table));
  size_t sample = 0;
  size_t run = 0;
  for (size_t chunk = 0;
       chunk < table.chunk_offsets.size() && sample < list.size(); ++chunk) {
    while (run + 1 < table.stsc.size() &&
           table.stsc[run + 1].first_chunk <= chunk + 1) {
      ++run;
    }
    const StscEntry& entry = table.stsc[run];
    uint64_t offset = table.chunk_offsets[chunk];
    for (uint32_t index = 0;
         index < entry.samples_per_chunk && sample < list.size(); ++index) {
      SampleRef& ref = list[sample];
      ref.offset = offset;
      ref.description = static_cast<uint16_t>(std::min<uint32_t>(
          entry.description_index, std::numeric_limits<uint16_t>::max()));
      if (std::numeric_limits<uint64_t>::max() - offset < ref.size) {
        return codec::Corrupt("sample offset overflows 64 bits");
      }
      offset += ref.size;
      ++sample;
    }
  }
  if (sample < list.size()) {
    return codec::Corrupt("chunk map covers fewer samples than stsz");
  }
  return absl::OkStatus();
}

void AssignSync(const RawSampleTable& table, SampleList& list) {
  if (!table.has_sync_table) {
    return;
  }
  for (SampleRef& ref : list) {
    ref.sync = false;
  }
  for (const uint32_t number : table.sync_samples) {
    if (number >= 1 && number <= list.size()) {
      list[number - 1].sync = true;
    }
  }
}

std::optional<int64_t> ToTrackTicks(uint64_t movie_ticks,
                                    uint32_t movie_timescale,
                                    uint32_t track_timescale) {
  if (movie_ticks > static_cast<uint64_t>(kMaxTimelineMagnitude)) {
    return std::nullopt;
  }
  const std::optional<penumbra::Rational> from =
      penumbra::Rational::Create(1, movie_timescale);
  const std::optional<penumbra::Rational> to =
      penumbra::Rational::Create(1, track_timescale);
  if (!from.has_value() || !to.has_value()) {
    return std::nullopt;
  }
  return penumbra::Rescale(static_cast<int64_t>(movie_ticks), *from, *to,
                           penumbra::Rounding::kNearest);
}

}

absl::StatusOr<std::shared_ptr<const SampleList>> BuildSampleList(
    const RawSampleTable& table, uint64_t sample_budget) {
  if (table.sample_count == 0) {
    return std::make_shared<const SampleList>();
  }
  if (table.sample_count > kMaxSamplesPerTrack ||
      table.sample_count > sample_budget) {
    return codec::Corrupt("sample count exceeds the supported limit");
  }
  auto list = std::make_shared<SampleList>(table.sample_count);
  AssignSizes(table, *list);
  ALUMBRA_RETURN_IF_ERROR(AssignDurations(table, *list));
  AssignCompositionOffsets(table, *list);
  ALUMBRA_RETURN_IF_ERROR(AssignOffsets(table, *list));
  AssignSync(table, *list);
  return std::shared_ptr<const SampleList>(std::move(list));
}

TrackTiming ComputeTiming(const TrackDef& track, uint32_t movie_timescale,
                          bool edit_lists_enabled) {
  TrackTiming timing;
  if (!edit_lists_enabled || track.edits.empty() || track.timescale == 0) {
    return timing;
  }
  const uint32_t movie_scale =
      movie_timescale == 0 ? track.timescale : movie_timescale;
  int64_t delay = 0;
  size_t index = 0;
  for (; index < track.edits.size() && track.edits[index].media_time < 0;
       ++index) {
    const std::optional<int64_t> ticks = ToTrackTicks(
        track.edits[index].segment_duration, movie_scale, track.timescale);
    if (!ticks.has_value() || delay > kMaxTimelineMagnitude - *ticks) {
      return TrackTiming{};
    }
    delay += *ticks;
  }
  if (index == track.edits.size()) {
    timing.shift = delay;
    return timing;
  }
  const EditEntry& first = track.edits[index];
  if (first.media_time > kMaxTimelineMagnitude) {
    return TrackTiming{};
  }
  timing.shift = delay - first.media_time;
  timing.ignored_entries = track.edits.size() - index - 1;
  if (first.segment_duration > 0) {
    const std::optional<int64_t> ticks =
        ToTrackTicks(first.segment_duration, movie_scale, track.timescale);
    if (ticks.has_value() && delay <= kMaxTimelineMagnitude - *ticks) {
      timing.end = delay + *ticks;
    }
  }
  return timing;
}

}
