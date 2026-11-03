#include "umbra/manifest/segment_index.h"

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
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/rational.h"
#include "umbra/manifest/ids.h"
#include "umbra/manifest/manifest_error.h"
#include "umbra/manifest/segment.h"
#include "umbra/manifest/url_template.h"

namespace umbra::manifest {
namespace {

using penumbra::MediaTime;
using penumbra::TimeDelta;

absl::Status CheckByteRange(const std::optional<ByteRange>& range) {
  if (!range.has_value()) {
    return absl::OkStatus();
  }
  std::int64_t end = 0;
  if (range->offset < 0 || range->length <= 0 ||
      __builtin_add_overflow(range->offset, range->length, &end)) {
    return InvalidManifest("a byte range is empty, negative or overflows");
  }
  return absl::OkStatus();
}

absl::Status CheckTables(const std::vector<InitSection>& inits,
                         const std::vector<KeyReference>& keys) {
  for (const InitSection& init : inits) {
    if (init.uri.empty()) {
      return InvalidManifest("an initialisation section has no URI");
    }
    if (absl::Status status = CheckByteRange(init.byte_range); !status.ok()) {
      return status;
    }
  }
  for (const KeyReference& key : keys) {
    if (key.uri.empty()) {
      return InvalidManifest("a key reference has no URI");
    }
  }
  return absl::OkStatus();
}

std::int64_t FirstSequenceOf(const ExplicitSegmentIndexParameters& parameters) {
  return parameters.segments.empty()
             ? parameters.first_sequence_number
             : parameters.segments.front().sequence_number;
}

bool SameIdentity(const Segment& a, const Segment& b) {
  return a.sequence_number == b.sequence_number && a.start == b.start &&
         a.duration == b.duration && a.uri == b.uri &&
         a.byte_range == b.byte_range;
}

std::size_t PositionOf(const SegmentIndex& index, std::int64_t sequence) {
  return static_cast<std::size_t>(sequence - index.FirstSequenceNumber());
}

template <typename T>
std::uint32_t Intern(std::vector<T>& target, const T& value) {
  for (std::size_t i = 0; i < target.size(); ++i) {
    if (target[i] == value) {
      return static_cast<std::uint32_t>(i);
    }
  }
  target.push_back(value);
  return static_cast<std::uint32_t>(target.size() - 1);
}

class Remapper {
 public:
  Remapper(const SegmentIndex& source, ExplicitSegmentIndexParameters& target)
      : source_(&source),
        target_(&target),
        init_map_(source.InitSections().size()),
        key_map_(source.KeyReferences().size()) {}

  Segment Apply(Segment segment) {
    if (segment.init_index.has_value()) {
      segment.init_index = Map(*segment.init_index, init_map_,
                               source_->InitSections(), target_->init_sections);
    }
    if (segment.key_index.has_value()) {
      segment.key_index =
          Map(*segment.key_index, key_map_, source_->KeyReferences(),
              target_->key_references);
    }
    return segment;
  }

 private:
  template <typename T>
  static std::uint32_t Map(std::uint32_t index,
                           std::vector<std::optional<std::uint32_t>>& cache,
                           const std::vector<T>& source,
                           std::vector<T>& target) {
    if (index >= cache.size()) {
      return index;
    }
    if (!cache[index].has_value()) {
      cache[index] = Intern(target, source[index]);
    }
    return cache[index].value_or(index);
  }

  const SegmentIndex* source_;
  ExplicitSegmentIndexParameters* target_;
  std::vector<std::optional<std::uint32_t>> init_map_;
  std::vector<std::optional<std::uint32_t>> key_map_;
};

}

std::int64_t SegmentIndex::LastSequenceNumber() const {
  return first_sequence_number_ + static_cast<std::int64_t>(count_) - 1;
}

MediaTime SegmentIndex::Start() const {
  return empty() ? MediaTime::Unknown() : StartOf(0);
}

MediaTime SegmentIndex::End() const {
  return empty() ? MediaTime::Unknown() : EndOf(count_ - 1);
}

std::optional<std::size_t> SegmentIndex::FindByTime(MediaTime time) const {
  if (empty() || !time.is_finite() || time < StartOf(0) ||
      time >= EndOf(count_ - 1)) {
    return std::nullopt;
  }
  std::size_t low = 0;
  std::size_t high = count_;
  while (low < high) {
    const std::size_t middle = low + ((high - low) / 2);
    if (StartOf(middle) <= time) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }
  const std::size_t position = low - 1;
  if (time < EndOf(position)) {
    return position;
  }
  return std::nullopt;
}

std::optional<std::size_t> SegmentIndex::FindBySequence(
    std::int64_t sequence_number) const {
  if (empty() || sequence_number < first_sequence_number_ ||
      sequence_number > LastSequenceNumber()) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(sequence_number - first_sequence_number_);
}

ExplicitSegmentIndex::ExplicitSegmentIndex(
    ExplicitSegmentIndexParameters parameters)
    : SegmentIndex(parameters.segments.size(), FirstSequenceOf(parameters),
                   std::move(parameters.init_sections),
                   std::move(parameters.key_references)),
      segments_(std::move(parameters.segments)) {}

absl::StatusOr<std::shared_ptr<const ExplicitSegmentIndex>>
ExplicitSegmentIndex::Create(ExplicitSegmentIndexParameters parameters) {
  if (parameters.segments.size() >
      static_cast<std::size_t>(kMaxSegmentsPerIndex)) {
    return InvalidManifest("a segment index has too many segments");
  }
  if (absl::Status status =
          CheckTables(parameters.init_sections, parameters.key_references);
      !status.ok()) {
    return status;
  }
  const std::int64_t first = FirstSequenceOf(parameters);
  std::int64_t last = 0;
  if (first < 0 ||
      __builtin_add_overflow(
          first, static_cast<std::int64_t>(parameters.segments.size()),
          &last)) {
    return InvalidManifest("segment sequence numbers are out of range");
  }
  const Segment* previous = nullptr;
  for (std::size_t i = 0; i < parameters.segments.size(); ++i) {
    const Segment& segment = parameters.segments[i];
    if (segment.sequence_number != first + static_cast<std::int64_t>(i)) {
      return InvalidManifest(absl::StrCat("segment sequence number ",
                                          segment.sequence_number,
                                          " is not contiguous"));
    }
    if (!segment.start.is_finite() || !segment.duration.is_finite() ||
        segment.duration.InMicroseconds() <= 0 || !segment.End().is_finite()) {
      return InvalidManifest(
          absl::StrCat("segment ", segment.sequence_number,
                       " has an unknown, infinite or empty time range"));
    }
    if (previous != nullptr &&
        (segment.start < previous->End() ||
         segment.discontinuity_sequence < previous->discontinuity_sequence)) {
      return InvalidManifest(
          absl::StrCat("segment ", segment.sequence_number,
                       " overlaps or precedes the segment before it"));
    }
    if (segment.uri.empty() && !segment.gap) {
      return InvalidManifest(absl::StrCat("segment ", segment.sequence_number,
                                          " has no URI and is not a gap"));
    }
    if (absl::Status status = CheckByteRange(segment.byte_range);
        !status.ok()) {
      return status;
    }
    if (segment.init_index.has_value() &&
        *segment.init_index >= parameters.init_sections.size()) {
      return InvalidManifest(
          absl::StrCat("segment ", segment.sequence_number,
                       " refers to a missing initialisation section"));
    }
    if (segment.key_index.has_value() &&
        *segment.key_index >= parameters.key_references.size()) {
      return InvalidManifest(absl::StrCat("segment ", segment.sequence_number,
                                          " refers to a missing key"));
    }
    previous = &segment;
  }
  return std::shared_ptr<const ExplicitSegmentIndex>(
      new ExplicitSegmentIndex(std::move(parameters)));
}

Segment ExplicitSegmentIndex::At(std::size_t position) const {
  return segments_[position];
}

MediaTime ExplicitSegmentIndex::StartOf(std::size_t position) const {
  return segments_[position].start;
}

MediaTime ExplicitSegmentIndex::EndOf(std::size_t position) const {
  return segments_[position].End();
}

bool ExplicitSegmentIndex::Equals(const SegmentIndex& other) const {
  const auto* that = dynamic_cast<const ExplicitSegmentIndex*>(&other);
  return that != nullptr &&
         FirstSequenceNumber() == that->FirstSequenceNumber() &&
         InitSections() == that->InitSections() &&
         KeyReferences() == that->KeyReferences() &&
         segments_ == that->segments_;
}

std::size_t ExplicitSegmentIndex::ApproximateBytes() const {
  std::size_t bytes = sizeof(*this) + (segments_.capacity() * sizeof(Segment));
  for (const Segment& segment : segments_) {
    bytes += segment.uri.capacity();
  }
  return bytes;
}

TemplateSegmentIndex::TemplateSegmentIndex(
    TemplateSegmentIndexParameters parameters, UrlTemplate media_template,
    std::vector<std::size_t> first_positions, std::size_t count)
    : SegmentIndex(count, parameters.first_sequence_number,
                   parameters.init_section.has_value()
                       ? std::vector<InitSection>{*parameters.init_section}
                       : std::vector<InitSection>{},
                   {}),
      media_template_(std::move(media_template)),
      init_section_(std::move(parameters.init_section)),
      representation_id_(std::move(parameters.representation_id)),
      bandwidth_(parameters.bandwidth),
      time_base_(parameters.time_base),
      runs_(std::move(parameters.runs)),
      first_positions_(std::move(first_positions)),
      presentation_time_offset_ticks_(
          parameters.presentation_time_offset_ticks),
      period_start_(parameters.period_start),
      discontinuity_sequence_(parameters.discontinuity_sequence) {}

absl::StatusOr<std::shared_ptr<const TemplateSegmentIndex>>
TemplateSegmentIndex::Create(TemplateSegmentIndexParameters parameters) {
  absl::StatusOr<UrlTemplate> media_template =
      UrlTemplate::Parse(parameters.media_template);
  if (!media_template.ok()) {
    return media_template.status();
  }
  if (!parameters.time_base.is_positive()) {
    return InvalidManifest("a segment template needs a positive time base");
  }
  if (parameters.presentation_time_offset_ticks < 0 ||
      !parameters.period_start.is_finite() ||
      parameters.representation_id.size() > kMaxIdBytes ||
      parameters.first_sequence_number < 0 || parameters.bandwidth < 0) {
    return InvalidManifest("a segment template parameter is out of range");
  }
  if (parameters.init_section.has_value()) {
    if (absl::Status status = CheckTables({*parameters.init_section},
                                          std::vector<KeyReference>{});
        !status.ok()) {
      return status;
    }
  }
  std::vector<std::size_t> first_positions;
  first_positions.reserve(parameters.runs.size());
  std::int64_t total = 0;
  std::int64_t previous_end = 0;
  for (const TimelineRun& run : parameters.runs) {
    std::int64_t span = 0;
    std::int64_t end = 0;
    if (run.count < 1 || run.duration_ticks < 1 || run.start_ticks < 0 ||
        run.start_ticks < previous_end ||
        __builtin_mul_overflow(run.duration_ticks, run.count, &span) ||
        __builtin_add_overflow(run.start_ticks, span, &end) ||
        __builtin_add_overflow(total, run.count, &total) ||
        total > kMaxSegmentsPerIndex) {
      return InvalidManifest(
          "a segment timeline run is empty, out of order or too large");
    }
    const std::optional<std::int64_t> micros = penumbra::Rescale(
        run.duration_ticks, parameters.time_base,
        penumbra::kMicrosecondTimeBase, penumbra::Rounding::kDown);
    if (!micros.has_value() || *micros < 1) {
      return InvalidManifest(
          "a segment duration is shorter than a microsecond");
    }
    first_positions.push_back(static_cast<std::size_t>(total - run.count));
    previous_end = end;
  }
  std::int64_t last_sequence = 0;
  if (__builtin_add_overflow(parameters.first_sequence_number, total,
                             &last_sequence)) {
    return InvalidManifest("segment sequence numbers are out of range");
  }
  const auto count = static_cast<std::size_t>(total);
  std::shared_ptr<const TemplateSegmentIndex> index(new TemplateSegmentIndex(
      std::move(parameters), *std::move(media_template),
      std::move(first_positions), count));
  if (!index->empty() &&
      (!index->Start().is_finite() || !index->End().is_finite())) {
    return InvalidManifest(
        "a segment template runs outside representable time");
  }
  return index;
}

std::size_t TemplateSegmentIndex::RunOf(std::size_t position) const {
  const auto it = std::ranges::upper_bound(first_positions_, position);
  return static_cast<std::size_t>(it - first_positions_.begin()) - 1;
}

std::int64_t TemplateSegmentIndex::StartTicks(std::size_t position) const {
  const std::size_t run = RunOf(position);
  return runs_[run].start_ticks +
         (static_cast<std::int64_t>(position - first_positions_[run]) *
          runs_[run].duration_ticks);
}

MediaTime TemplateSegmentIndex::TicksToTime(std::int64_t ticks) const {
  const std::optional<std::int64_t> micros = penumbra::Rescale(
      ticks - presentation_time_offset_ticks_, time_base_,
      penumbra::kMicrosecondTimeBase, penumbra::Rounding::kDown);
  if (!micros.has_value()) {
    return MediaTime::Unknown();
  }
  return period_start_ + TimeDelta::FromMicroseconds(*micros);
}

MediaTime TemplateSegmentIndex::StartOf(std::size_t position) const {
  return TicksToTime(StartTicks(position));
}

MediaTime TemplateSegmentIndex::EndOf(std::size_t position) const {
  return TicksToTime(StartTicks(position) +
                     runs_[RunOf(position)].duration_ticks);
}

Segment TemplateSegmentIndex::At(std::size_t position) const {
  Segment segment;
  segment.sequence_number =
      FirstSequenceNumber() + static_cast<std::int64_t>(position);
  segment.start = StartOf(position);
  segment.duration = EndOf(position) - segment.start;
  TemplateValues values;
  values.number = segment.sequence_number;
  values.time = StartTicks(position);
  values.bandwidth = bandwidth_;
  values.representation_id = representation_id_;
  segment.uri = media_template_.Expand(values);
  if (init_section_.has_value()) {
    segment.init_index = 0;
  }
  segment.discontinuity_sequence = discontinuity_sequence_;
  return segment;
}

bool TemplateSegmentIndex::Equals(const SegmentIndex& other) const {
  const auto* that = dynamic_cast<const TemplateSegmentIndex*>(&other);
  return that != nullptr &&
         FirstSequenceNumber() == that->FirstSequenceNumber() &&
         media_template_ == that->media_template_ &&
         init_section_ == that->init_section_ &&
         representation_id_ == that->representation_id_ &&
         bandwidth_ == that->bandwidth_ && time_base_ == that->time_base_ &&
         runs_ == that->runs_ &&
         presentation_time_offset_ticks_ ==
             that->presentation_time_offset_ticks_ &&
         period_start_ == that->period_start_ &&
         discontinuity_sequence_ == that->discontinuity_sequence_;
}

std::size_t TemplateSegmentIndex::ApproximateBytes() const {
  return sizeof(*this) + (runs_.capacity() * sizeof(TimelineRun)) +
         (first_positions_.capacity() * sizeof(std::size_t)) +
         media_template_.Text().capacity() + representation_id_.capacity();
}

SegmentIndexProvider SegmentIndexProvider::Resolved(
    std::shared_ptr<const SegmentIndex> index) {
  SegmentIndexProvider provider;
  provider.index_ = std::move(index);
  return provider;
}

SegmentIndexProvider SegmentIndexProvider::Deferred(IndexLocation location) {
  SegmentIndexProvider provider;
  provider.location_ = std::move(location);
  return provider;
}

SegmentIndexProvider SegmentIndexProvider::WithIndex(
    std::shared_ptr<const SegmentIndex> index) const {
  SegmentIndexProvider provider = *this;
  provider.index_ = std::move(index);
  return provider;
}

bool operator==(const SegmentIndexProvider& a, const SegmentIndexProvider& b) {
  if (a.location_ != b.location_) {
    return false;
  }
  if (a.index_ == b.index_) {
    return true;
  }
  return a.index_ != nullptr && b.index_ != nullptr &&
         a.index_->Equals(*b.index_);
}

std::optional<std::int64_t> SequenceForStart(const SegmentIndex& previous,
                                             MediaTime start) {
  const std::optional<std::size_t> position = previous.FindByTime(start);
  if (!position.has_value() || previous.StartOf(*position) != start) {
    return std::nullopt;
  }
  return previous.FirstSequenceNumber() + static_cast<std::int64_t>(*position);
}

absl::StatusOr<std::shared_ptr<const SegmentIndex>> MergeSegmentIndexes(
    const std::shared_ptr<const SegmentIndex>& previous,
    const std::shared_ptr<const SegmentIndex>& next) {
  if (previous == nullptr || previous->empty() || next == nullptr ||
      next->empty()) {
    return next;
  }
  if (previous->Equals(*next)) {
    return previous;
  }
  if (next->FirstSequenceNumber() < previous->FirstSequenceNumber()) {
    return InvalidManifest("a refresh moved the segment window backwards");
  }
  if (next->FirstSequenceNumber() > previous->LastSequenceNumber()) {
    return next;
  }
  if (next->LastSequenceNumber() < previous->LastSequenceNumber()) {
    return InvalidManifest("a refresh removed segments from the window end");
  }
  const std::int64_t overlap_first = next->FirstSequenceNumber();
  const std::int64_t overlap_last = previous->LastSequenceNumber();
  for (std::int64_t sequence = overlap_first; sequence <= overlap_last;
       ++sequence) {
    const Segment before = previous->At(PositionOf(*previous, sequence));
    const Segment after = next->At(PositionOf(*next, sequence));
    if (!SameIdentity(before, after)) {
      return InvalidManifest(
          absl::StrCat("a refresh changed the identity of segment ", sequence));
    }
  }
  if (next->IndexKind() == SegmentIndex::Kind::kTemplate) {
    return next;
  }
  ExplicitSegmentIndexParameters merged;
  merged.first_sequence_number = next->FirstSequenceNumber();
  Remapper from_previous(*previous, merged);
  Remapper from_next(*next, merged);
  merged.segments.reserve(next->size());
  for (std::size_t i = 0; i < next->size(); ++i) {
    const std::int64_t sequence =
        next->FirstSequenceNumber() + static_cast<std::int64_t>(i);
    if (sequence <= overlap_last) {
      merged.segments.push_back(
          from_previous.Apply(previous->At(PositionOf(*previous, sequence))));
    } else {
      merged.segments.push_back(from_next.Apply(next->At(i)));
    }
  }
  absl::StatusOr<std::shared_ptr<const ExplicitSegmentIndex>> created =
      ExplicitSegmentIndex::Create(std::move(merged));
  if (!created.ok()) {
    return created.status();
  }
  return std::shared_ptr<const SegmentIndex>(*std::move(created));
}

}
