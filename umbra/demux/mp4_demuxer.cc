#include "umbra/demux/mp4_demuxer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/base/status.h"
#include "umbra/codec/reader_util.h"
#include "umbra/demux/byte_queue.h"
#include "umbra/demux/caption_extractor.h"
#include "umbra/demux/demuxer.h"
#include "umbra/demux/mp4_box.h"
#include "umbra/demux/mp4_fragment.h"
#include "umbra/demux/mp4_seek_map.h"
#include "umbra/demux/mp4_tables.h"
#include "umbra/demux/mp4_types.h"

namespace umbra::demux {
namespace {

using Advance = absl::StatusOr<std::optional<StepResult>>;

constexpr uint64_t kUnbounded = std::numeric_limits<uint64_t>::max();
constexpr int64_t kMaxTimeline = int64_t{1} << 61;
constexpr uint32_t kUnknownEventDuration = 0xFFFFFFFFU;
constexpr std::string_view kAomId3Scheme = "https://aomedia.org/emsg/ID3";

absl::Status Truncated() { return codec::Corrupt("the file is truncated"); }

Advance Continue() { return std::optional<StepResult>(); }

Advance Return(StepResult result) { return std::optional<StepResult>(result); }

bool IsBufferedBox(uint32_t type) {
  return type == kBoxMoov || type == kBoxMoof || type == kBoxSidx ||
         type == kBoxEmsg || type == kBoxPrft;
}

const penumbra::StreamInfo* PrimaryStream(const TrackDef& def) {
  for (const SampleEntryInfo& entry : def.entries) {
    if (entry.stream.has_value()) {
      return &*entry.stream;
    }
  }
  return nullptr;
}

std::optional<int64_t> ToMicros(uint64_t value, uint32_t timescale) {
  if (timescale == 0 || value > static_cast<uint64_t>(kMaxTimeline)) {
    return std::nullopt;
  }
  const std::optional<penumbra::Rational> base =
      penumbra::Rational::Create(1, timescale);
  if (!base.has_value()) {
    return std::nullopt;
  }
  return penumbra::Rescale(static_cast<int64_t>(value), *base,
                           penumbra::kMicrosecondTimeBase,
                           penumbra::Rounding::kNearest);
}

penumbra::Timestamp MakeTimestamp(int64_t value, penumbra::Rational base) {
  penumbra::Timestamp timestamp;
  timestamp.value = value;
  timestamp.time_base = base;
  return timestamp;
}

std::optional<penumbra::Rational> TimeBaseOf(uint32_t timescale) {
  return penumbra::Rational::Create(1, timescale);
}

}

Mp4Demuxer::Mp4Demuxer(DemuxerOutput& output, Mp4DemuxerOptions options)
    : output_(output), options_(options) {}

void Mp4Demuxer::SetSourceInfo(std::optional<uint64_t> size, bool seekable) {
  source_size_ = size;
  seekable_ = seekable;
}

uint64_t Mp4Demuxer::NextOffset() const { return queue_.EndOffset(); }

absl::Status Mp4Demuxer::Append(uint64_t offset, penumbra::SharedBytes bytes) {
  if (offset != queue_.EndOffset()) {
    return absl::FailedPreconditionError(
        absl::StrCat("bytes appended at offset ", offset,
                     " but the demuxer expects ", queue_.EndOffset()));
  }
  queue_.Append(std::move(bytes));
  return absl::OkStatus();
}

void Mp4Demuxer::EndOfInput() { ended_ = true; }

absl::StatusOr<StepResult> Mp4Demuxer::Step() {
  if (!failure_.ok()) {
    return failure_;
  }
  if (finished_) {
    return StepResult::EndOfStream();
  }
  for (size_t unit = 0; unit < kStepBudgetUnits; ++unit) {
    Advance advance = sample_phase_ ? AdvanceSamples() : AdvanceBoxes();
    if (!advance.ok()) {
      failure_ = advance.status();
      return failure_;
    }
    if (advance->has_value()) {
      return **advance;
    }
  }
  return StepResult::Yield();
}

Mp4Demuxer::Advance Mp4Demuxer::NeedMore(size_t wanted) {
  if (ended_) {
    return Truncated();
  }
  return Return(StepResult::NeedMoreData(queue_.EndOffset(),
                                         std::max<size_t>(wanted, 1)));
}

Mp4Demuxer::Advance Mp4Demuxer::MoveTo(uint64_t target) {
  const uint64_t start = queue_.StartOffset();
  const uint64_t end = queue_.EndOffset();
  if (target == start) {
    return Continue();
  }
  if (target > start && target <= end) {
    queue_.Discard(target - start);
    return Continue();
  }
  if (target > end && target - end <= kMaxSkipBytes) {
    queue_.Discard(queue_.size());
    if (ended_) {
      return Truncated();
    }
    return Return(StepResult::NeedMoreData(end, target - end));
  }
  queue_.Reset(target);
  ended_ = false;
  return Return(StepResult::Seek(target));
}

Mp4Demuxer::Advance Mp4Demuxer::AdvanceBoxes() {
  if (next_box_ == kUnbounded) {
    queue_.Discard(queue_.size());
    if (ended_) {
      return FinishStream();
    }
    return Return(StepResult::NeedMoreData(queue_.EndOffset(), 1));
  }
  ALUMBRA_ASSIGN_OR_RETURN(const std::optional<StepResult> moved,
                           MoveTo(next_box_));
  if (moved.has_value()) {
    return Return(*moved);
  }
  if (queue_.size() < kMinBoxHeaderBytes) {
    if (ended_) {
      return FinishStream();
    }
    return NeedMore(kMinBoxHeaderBytes - queue_.size());
  }
  const std::vector<uint8_t> prefix = queue_.Peek(kMaxBoxHeaderBytes);
  const size_t needed = BoxHeaderBytes(prefix);
  if (prefix.size() < needed) {
    return NeedMore(needed - prefix.size());
  }
  ALUMBRA_ASSIGN_OR_RETURN(const BoxHeader header, ParseBoxHeader(prefix));
  if (!saw_box_ && !IsKnownTopLevelBox(header.type)) {
    return codec::Corrupt("the file does not start with an ISO box");
  }
  saw_box_ = true;
  return HandleBox(header);
}

Mp4Demuxer::Advance Mp4Demuxer::FinishStream() {
  if (!moov_seen_) {
    return codec::Corrupt("the file has no moov box");
  }
  EmitPendingEmsgUnknown();
  finished_ = true;
  return Return(StepResult::EndOfStream());
}

Mp4Demuxer::Advance Mp4Demuxer::HandleBox(const BoxHeader& header) {
  const uint64_t start = queue_.StartOffset();
  uint64_t total = header.total_size;
  if (total == 0) {
    if (source_size_.has_value() && *source_size_ > start) {
      total = *source_size_ - start;
    } else if (header.type == kBoxMdat) {
      total = kUnbounded;
    } else {
      return FinishStream();
    }
  }
  if (total != kUnbounded && total > kUnbounded - start) {
    return codec::Corrupt("box size overflows the file offset");
  }
  if (header.type == kBoxMdat) {
    return HandleMdat(start, total);
  }
  if (IsBufferedBox(header.type)) {
    return ReadWholeBox(header, total);
  }
  next_box_ = start + total;
  return Continue();
}

Mp4Demuxer::Advance Mp4Demuxer::HandleMdat(uint64_t start, uint64_t total) {
  const uint64_t end = total == kUnbounded ? kUnbounded : start + total;
  if (moov_seen_ && fragment_pending_) {
    fragment_pending_ = false;
    sample_phase_ = true;
    resume_box_ = end;
    return Continue();
  }
  next_box_ = end;
  return Continue();
}

Mp4Demuxer::Advance Mp4Demuxer::ReadWholeBox(const BoxHeader& header,
                                             uint64_t total) {
  if (total > kMaxBufferedBoxBytes) {
    return codec::Corrupt(
        absl::StrCat("box of ", total, " bytes exceeds the buffering limit"));
  }
  const auto size = static_cast<size_t>(total);
  if (queue_.size() < size) {
    return NeedMore(size - queue_.size());
  }
  const uint64_t start = queue_.StartOffset();
  const std::optional<penumbra::SharedBytes> box = queue_.Read(size);
  if (!box.has_value()) {
    return Truncated();
  }
  next_box_ = start + total;
  ALUMBRA_RETURN_IF_ERROR(
      DispatchBox(header.type, *box, header.header_size, start));
  return Continue();
}

absl::Status Mp4Demuxer::DispatchBox(uint32_t type,
                                     const penumbra::SharedBytes& box,
                                     size_t header_size, uint64_t start) {
  const std::span<const uint8_t> payload = box.span().subspan(header_size);
  if (type == kBoxMoov) {
    return OnMoov(payload);
  }
  if (type == kBoxMoof) {
    return OnMoof(payload, start, box.size());
  }
  if (type == kBoxSidx) {
    return OnSidx(payload, start + box.size());
  }
  if (type == kBoxEmsg) {
    return OnEmsg(payload);
  }
  return OnPrft(payload);
}

size_t Mp4Demuxer::NextTrack() const {
  size_t best = tracks_.size();
  for (size_t index = 0; index < tracks_.size(); ++index) {
    const Track& track = tracks_[index];
    if (track.cursor >= track.samples->size()) {
      continue;
    }
    if (best == tracks_.size() ||
        (*track.samples)[track.cursor].offset <
            (*tracks_[best].samples)[tracks_[best].cursor].offset) {
      best = index;
    }
  }
  return best;
}

Mp4Demuxer::Advance Mp4Demuxer::AdvanceSamples() {
  const size_t next = NextTrack();
  if (next == tracks_.size()) {
    if (progressive_) {
      return FinishStream();
    }
    sample_phase_ = false;
    next_box_ = resume_box_;
    return Continue();
  }
  Track& track = tracks_[next];
  const SampleRef ref = (*track.samples)[track.cursor];
  if (ref.size > kMaxSampleBytes) {
    return codec::Corrupt(absl::StrCat("sample of ", ref.size,
                                       " bytes exceeds the sample limit"));
  }
  ALUMBRA_ASSIGN_OR_RETURN(const std::optional<StepResult> moved,
                           MoveTo(ref.offset));
  if (moved.has_value()) {
    return Return(*moved);
  }
  if (queue_.size() < ref.size) {
    return NeedMore(ref.size - queue_.size());
  }
  std::optional<penumbra::SharedBytes> data = queue_.Read(ref.size);
  if (!data.has_value()) {
    return Truncated();
  }
  ++track.cursor;
  ALUMBRA_RETURN_IF_ERROR(EmitSample(track, ref, *std::move(data)));
  return Continue();
}

const SampleEntryInfo* Mp4Demuxer::EntryFor(const Track& track,
                                            uint16_t description) const {
  const TrackDef& def = movie_.tracks[track.def_index];
  if (description < 1 || description > def.entries.size()) {
    return nullptr;
  }
  return &def.entries[description - 1];
}

absl::Status Mp4Demuxer::EmitSample(Track& track, const SampleRef& ref,
                                    penumbra::SharedBytes data) {
  const SampleEntryInfo* entry = EntryFor(track, ref.description);
  if (entry == nullptr) {
    return codec::Corrupt("sample refers to a missing sample description");
  }
  if (!entry->stream.has_value()) {
    return absl::OkStatus();
  }
  const TrackDef& def = movie_.tracks[track.def_index];
  const penumbra::Rational base = entry->stream->time_base;
  penumbra::EncodedSample sample;
  sample.track_id = def.track_id;
  sample.data = std::move(data);
  const int64_t dts = ref.dts + track.timing.shift;
  const int64_t pts = dts + ref.cts_offset;
  sample.dts = MakeTimestamp(dts, base);
  sample.pts = MakeTimestamp(pts, base);
  sample.duration = MakeTimestamp(ref.duration, base);
  sample.key_frame = ref.sync;
  sample.discontinuity = track.discontinuity_pending;
  track.discontinuity_pending = false;
  if (track.config_change_pending ||
      (track.current_description != 0 &&
       track.current_description != ref.description)) {
    sample.config_change =
        std::make_shared<const penumbra::StreamInfo>(*entry->stream);
  }
  track.config_change_pending = false;
  track.current_description = ref.description;
  if (std::holds_alternative<penumbra::AudioInfo>(entry->stream->format)) {
    const int64_t duration = ref.duration;
    if (pts < 0) {
      sample.trim_start = MakeTimestamp(std::min(duration, -pts), base);
    }
    if (track.timing.end.has_value() && pts + duration > *track.timing.end) {
      sample.trim_end = MakeTimestamp(
          std::min(duration, pts + duration - *track.timing.end), base);
    }
  }
  if (entry->caption_codec.has_value()) {
    std::vector<uint8_t> metadata = ExtractHdrDynamicMetadata(
        *entry->caption_codec, entry->nal_length_size, sample.data.span());
    if (!metadata.empty()) {
      sample.hdr_dynamic_metadata =
          penumbra::SharedBytes::Adopt(std::move(metadata));
    }
  }
  EmitCaptions(track, *entry, sample);
  output_.OnSample(std::move(sample));
  return absl::OkStatus();
}

void Mp4Demuxer::EmitCaptions(Track& track, const SampleEntryInfo& entry,
                              const penumbra::EncodedSample& sample) {
  if (!options_.extract_captions || !entry.caption_codec.has_value()) {
    return;
  }
  std::vector<std::vector<uint8_t>> payloads = ExtractCaptionData(
      *entry.caption_codec, entry.nal_length_size, sample.data.span());
  for (std::vector<uint8_t>& payload : payloads) {
    if (track.caption_stream_id == 0) {
      MetadataStreamInfo stream;
      stream.stream_id = next_metadata_stream_id_++;
      stream.kind = MetadataKind::kCaptions;
      stream.time_base = sample.pts.time_base;
      stream.source_track_id = sample.track_id;
      track.caption_stream_id = stream.stream_id;
      output_.OnMetadataStream(stream);
    }
    TimedMetadataItem item;
    item.stream_id = track.caption_stream_id;
    item.time = sample.pts;
    item.duration = sample.duration;
    item.payload = penumbra::SharedBytes::Adopt(std::move(payload));
    output_.OnTimedMetadata(std::move(item));
  }
}

absl::Status Mp4Demuxer::OnMoov(std::span<const uint8_t> payload) {
  ALUMBRA_ASSIGN_OR_RETURN(Movie movie, ParseMovie(payload));
  ALUMBRA_RETURN_IF_ERROR(BuildTracks(std::move(movie)));
  AnnounceMovie();
  if (progressive_) {
    sample_phase_ = true;
  }
  return absl::OkStatus();
}

absl::Status Mp4Demuxer::BuildTracks(Movie movie) {
  std::vector<Track> built;
  std::vector<std::optional<int64_t>> expected(movie.tracks.size());
  uint64_t budget = kMaxSamplesPerMovie;
  bool any_samples = false;
  for (size_t index = 0; index < movie.tracks.size(); ++index) {
    const TrackDef& def = movie.tracks[index];
    const bool playable = std::ranges::any_of(
        def.entries,
        [](const SampleEntryInfo& entry) { return entry.stream.has_value(); });
    if (!playable) {
      ++stats_.skipped_tracks;
      continue;
    }
    if (def.timescale == 0) {
      return codec::Corrupt("track has a zero timescale");
    }
    Track track;
    track.def_index = index;
    ALUMBRA_ASSIGN_OR_RETURN(track.samples, BuildSampleList(def.table, budget));
    budget -= track.samples->size();
    any_samples = any_samples || !track.samples->empty();
    track.timing =
        ComputeTiming(def, movie.movie_timescale, options_.edit_lists_enabled);
    stats_.ignored_edit_entries += track.timing.ignored_entries;
    track.config_change_pending = moov_seen_;
    built.push_back(std::move(track));
  }
  if (built.empty()) {
    return codec::Unsupported("the file has no supported track");
  }
  movie_ = std::move(movie);
  tracks_ = std::move(built);
  expected_dts_ = std::move(expected);
  progressive_ = any_samples;
  moov_seen_ = true;
  return absl::OkStatus();
}

void Mp4Demuxer::AnnounceMovie() {
  std::vector<penumbra::StreamInfo> streams;
  std::vector<SeekTrack> seek_tracks;
  bool reference_set = false;
  for (const Track& track : tracks_) {
    const TrackDef& def = movie_.tracks[track.def_index];
    const penumbra::StreamInfo* primary_stream = PrimaryStream(def);
    if (primary_stream == nullptr) {
      continue;
    }
    const penumbra::StreamInfo& primary = *primary_stream;
    streams.push_back(primary);
    SeekTrack seek_track;
    seek_track.samples = track.samples;
    seek_track.shift = track.timing.shift;
    seek_track.time_base = primary.time_base;
    const bool video = primary.type() == penumbra::StreamType::kVideo;
    seek_track.reference = video && !reference_set;
    reference_set = reference_set || seek_track.reference;
    seek_tracks.push_back(std::move(seek_track));
  }
  if (!reference_set && !seek_tracks.empty()) {
    seek_tracks.front().reference = true;
  }
  output_.OnStreams(std::move(streams));
  penumbra::TimeDelta duration = penumbra::TimeDelta::Unknown();
  const uint64_t ticks = movie_.movie_duration != 0 ? movie_.movie_duration
                                                    : movie_.fragment_duration;
  const std::optional<int64_t> micros = ToMicros(ticks, movie_.movie_timescale);
  if (micros.has_value() && ticks != 0) {
    duration = penumbra::TimeDelta::FromMicroseconds(*micros);
  }
  if (progressive_) {
    output_.OnSeekMap(
        std::make_shared<ProgressiveSeekMap>(std::move(seek_tracks), duration));
  } else {
    output_.OnSeekMap(
        std::make_shared<IndexedSeekMap>(std::vector<SeekPoint>(), duration));
  }
}

absl::Status Mp4Demuxer::OnMoof(std::span<const uint8_t> payload,
                                uint64_t start, uint64_t total) {
  if (!moov_seen_) {
    return codec::Corrupt("moof appears before moov");
  }
  FragmentContext context;
  context.tracks = &movie_.tracks;
  context.expected_dts = &expected_dts_;
  context.sample_budget = kMaxSamplesPerMovie;
  ALUMBRA_ASSIGN_OR_RETURN(Fragment fragment,
                           ParseMoof(payload, start, total, context));
  FlushPendingEmsg(fragment);
  uint64_t min_offset = kUnbounded;
  bool any = false;
  for (const TrackFragment& track : fragment.tracks) {
    for (const SampleRef& sample : track.samples) {
      min_offset = std::min(min_offset, sample.offset);
      any = true;
    }
  }
  ApplyFragment(std::move(fragment));
  if (!any) {
    return absl::OkStatus();
  }
  if (min_offset < start + total) {
    sample_phase_ = true;
    resume_box_ = start + total;
  } else {
    fragment_pending_ = true;
  }
  return absl::OkStatus();
}

void Mp4Demuxer::ApplyFragment(Fragment fragment) {
  const auto empty = std::make_shared<const SampleList>();
  for (Track& track : tracks_) {
    track.samples = empty;
    track.cursor = 0;
  }
  for (TrackFragment& incoming : fragment.tracks) {
    for (Track& track : tracks_) {
      if (movie_.tracks[track.def_index].track_id != incoming.track_id) {
        continue;
      }
      std::optional<int64_t>& expected = expected_dts_[track.def_index];
      if (incoming.has_decode_time &&
          expected.value_or(incoming.first_dts) != incoming.first_dts) {
        track.discontinuity_pending = true;
      }
      if (!incoming.samples.empty()) {
        expected =
            incoming.samples.back().dts + incoming.samples.back().duration;
        track.samples =
            std::make_shared<const SampleList>(std::move(incoming.samples));
      }
      break;
    }
  }
}

absl::Status Mp4Demuxer::OnSidx(std::span<const uint8_t> payload,
                                uint64_t box_end) {
  ALUMBRA_ASSIGN_OR_RETURN(const Sidx sidx, ParseSidx(payload));
  if (sidx.timescale == 0) {
    return codec::Corrupt("sidx has a zero timescale");
  }
  if (!moov_seen_) {
    return absl::OkStatus();
  }
  if (index_reference_id_.has_value() &&
      *index_reference_id_ != sidx.reference_id) {
    return absl::OkStatus();
  }
  const bool known_track =
      std::ranges::any_of(tracks_, [&](const Track& track) {
        return movie_.tracks[track.def_index].track_id == sidx.reference_id;
      });
  if (!known_track) {
    return absl::OkStatus();
  }
  index_reference_id_ = sidx.reference_id;
  if (sidx.first_offset > kUnbounded - box_end) {
    return codec::Corrupt("sidx first offset overflows the file offset");
  }
  uint64_t offset = box_end + sidx.first_offset;
  uint64_t time = sidx.earliest_presentation_time;
  for (const SidxReference& reference : sidx.references) {
    const std::optional<int64_t> micros = ToMicros(time, sidx.timescale);
    if (!micros.has_value()) {
      return codec::Corrupt("sidx time is out of range");
    }
    index_points_[*micros] =
        SeekPoint{.time = penumbra::MediaTime::FromMicroseconds(*micros),
                  .byte_offset = offset};
    time += reference.duration;
    if (kUnbounded - offset < reference.size) {
      return codec::Corrupt("sidx reference overflows the file offset");
    }
    offset += reference.size;
  }
  const std::optional<int64_t> end_micros = ToMicros(time, sidx.timescale);
  if (end_micros.has_value()) {
    index_end_micros_ = std::max(index_end_micros_, *end_micros);
  }
  std::vector<SeekPoint> points;
  points.reserve(index_points_.size());
  for (const auto& [key, point] : index_points_) {
    points.push_back(point);
  }
  output_.OnSeekMap(std::make_shared<IndexedSeekMap>(
      std::move(points),
      penumbra::TimeDelta::FromMicroseconds(index_end_micros_)));
  return absl::OkStatus();
}

uint32_t Mp4Demuxer::EmsgStreamFor(const Emsg& emsg) {
  const auto key = std::make_pair(emsg.scheme, emsg.value);
  const auto found = emsg_streams_.find(key);
  if (found != emsg_streams_.end()) {
    return found->second;
  }
  MetadataStreamInfo stream;
  stream.stream_id = next_metadata_stream_id_++;
  stream.kind =
      emsg.scheme == kAomId3Scheme ? MetadataKind::kId3 : MetadataKind::kEmsg;
  stream.time_base = TimeBaseOf(emsg.timescale).value_or(penumbra::Rational());
  stream.scheme = emsg.scheme;
  stream.value = emsg.value;
  emsg_streams_[key] = stream.stream_id;
  output_.OnMetadataStream(stream);
  return stream.stream_id;
}

void Mp4Demuxer::EmitEmsg(uint32_t stream_id, const Emsg& emsg,
                          penumbra::Timestamp time) {
  const penumbra::Rational base =
      TimeBaseOf(emsg.timescale).value_or(penumbra::Rational());
  TimedMetadataItem item;
  item.stream_id = stream_id;
  item.time = time;
  if (emsg.event_duration != kUnknownEventDuration) {
    item.duration = MakeTimestamp(emsg.event_duration, base);
  }
  item.payload = penumbra::SharedBytes::Copy(emsg.message);
  item.event_id = emsg.id;
  output_.OnTimedMetadata(std::move(item));
}

absl::Status Mp4Demuxer::OnEmsg(std::span<const uint8_t> payload) {
  absl::StatusOr<Emsg> parsed = ParseEmsg(payload);
  if (!parsed.ok()) {
    if (parsed.status().code() == absl::StatusCode::kUnimplemented) {
      ++stats_.unsupported_emsg_boxes;
      return absl::OkStatus();
    }
    return parsed.status();
  }
  if (parsed->timescale == 0 ||
      parsed->time > static_cast<uint64_t>(kMaxTimeline)) {
    return codec::Corrupt("emsg timing is out of range");
  }
  const uint32_t stream_id = EmsgStreamFor(*parsed);
  if (parsed->version == 1) {
    const penumbra::Rational base =
        TimeBaseOf(parsed->timescale).value_or(penumbra::Rational());
    EmitEmsg(stream_id, *parsed,
             MakeTimestamp(static_cast<int64_t>(parsed->time), base));
    return absl::OkStatus();
  }
  if (pending_emsg_.size() >= kMaxPendingEmsg) {
    return codec::Corrupt("too many emsg boxes wait for a fragment");
  }
  PendingEmsg pending;
  pending.stream_id = stream_id;
  pending.emsg = std::move(*parsed);
  pending_emsg_.push_back(std::move(pending));
  return absl::OkStatus();
}

void Mp4Demuxer::FlushPendingEmsg(const Fragment& fragment) {
  if (pending_emsg_.empty()) {
    return;
  }
  std::optional<int64_t> earliest;
  for (const TrackFragment& incoming : fragment.tracks) {
    for (const Track& track : tracks_) {
      const TrackDef& def = movie_.tracks[track.def_index];
      if (def.track_id != incoming.track_id) {
        continue;
      }
      int64_t first = incoming.first_dts;
      for (const SampleRef& sample : incoming.samples) {
        first = std::min(first, sample.dts + sample.cts_offset);
      }
      const std::optional<penumbra::Rational> base = TimeBaseOf(def.timescale);
      if (!base.has_value()) {
        continue;
      }
      const std::optional<int64_t> micros = penumbra::Rescale(
          first + track.timing.shift, *base, penumbra::kMicrosecondTimeBase,
          penumbra::Rounding::kNearest);
      if (micros.has_value() &&
          (!earliest.has_value() || *micros < *earliest)) {
        earliest = micros;
      }
    }
  }
  for (const PendingEmsg& pending : pending_emsg_) {
    penumbra::Timestamp time;
    const std::optional<penumbra::Rational> base =
        TimeBaseOf(pending.emsg.timescale);
    if (earliest.has_value() && base.has_value()) {
      const std::optional<int64_t> ticks =
          penumbra::Rescale(*earliest, penumbra::kMicrosecondTimeBase, *base,
                            penumbra::Rounding::kNearest);
      if (ticks.has_value()) {
        time = MakeTimestamp(*ticks + static_cast<int64_t>(pending.emsg.time),
                             *base);
      }
    }
    EmitEmsg(pending.stream_id, pending.emsg, time);
  }
  pending_emsg_.clear();
}

void Mp4Demuxer::EmitPendingEmsgUnknown() {
  for (const PendingEmsg& pending : pending_emsg_) {
    EmitEmsg(pending.stream_id, pending.emsg, penumbra::Timestamp());
  }
  pending_emsg_.clear();
}

absl::Status Mp4Demuxer::OnPrft(std::span<const uint8_t> payload) {
  ALUMBRA_ASSIGN_OR_RETURN(const Prft prft, ParsePrft(payload));
  for (const TrackDef& def : movie_.tracks) {
    if (def.track_id != prft.track_id) {
      continue;
    }
    const std::optional<penumbra::Rational> base = TimeBaseOf(def.timescale);
    if (!base.has_value() ||
        prft.media_time > static_cast<uint64_t>(kMaxTimeline)) {
      return absl::OkStatus();
    }
    ProducerReferenceTime reference;
    reference.track_id = prft.track_id;
    reference.ntp_timestamp = prft.ntp_timestamp;
    reference.media_time =
        MakeTimestamp(static_cast<int64_t>(prft.media_time), *base);
    reference.flags = prft.flags;
    output_.OnProducerReferenceTime(reference);
    return absl::OkStatus();
  }
  return absl::OkStatus();
}

void Mp4Demuxer::ResetState(uint64_t offset) {
  queue_.Reset(offset);
  next_box_ = offset;
  ended_ = false;
  finished_ = false;
  failure_ = absl::OkStatus();
  fragment_pending_ = false;
  sample_phase_ = false;
  pending_emsg_.clear();
  for (Track& track : tracks_) {
    track.discontinuity_pending = true;
    if (!progressive_) {
      track.samples = std::make_shared<const SampleList>();
      track.cursor = 0;
    }
  }
  if (!progressive_) {
    std::ranges::fill(expected_dts_, std::nullopt);
  }
}

void Mp4Demuxer::Reset(uint64_t offset) {
  ResetState(offset);
  sample_phase_ = progressive_;
}

absl::StatusOr<uint64_t> Mp4Demuxer::Seek(const SeekPoint& point) {
  if (!moov_seen_) {
    return absl::FailedPreconditionError("seek before the movie is parsed");
  }
  if (!progressive_) {
    ResetState(point.byte_offset);
    return point.byte_offset;
  }
  if (!point.time.is_finite()) {
    return absl::InvalidArgumentError("seek needs a finite time");
  }
  uint64_t offset = kUnbounded;
  for (Track& track : tracks_) {
    track.cursor = track.samples->size();
    const TrackDef& def = movie_.tracks[track.def_index];
    const std::optional<penumbra::Rational> base = TimeBaseOf(def.timescale);
    if (!base.has_value() || track.samples->empty()) {
      continue;
    }
    const std::optional<int64_t> ticks = penumbra::Rescale(
        point.time.InMicroseconds(), penumbra::kMicrosecondTimeBase, *base,
        penumbra::Rounding::kDown);
    if (!ticks.has_value()) {
      continue;
    }
    track.cursor = FindSeekIndex(*track.samples, track.timing.shift, *ticks);
    offset = std::min(offset, (*track.samples)[track.cursor].offset);
  }
  ResetState(offset == kUnbounded ? 0 : offset);
  sample_phase_ = true;
  return queue_.StartOffset();
}

}
