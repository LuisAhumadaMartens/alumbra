#include "umbra/demux/mkv_demuxer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/base/status.h"
#include "umbra/codec/reader_util.h"
#include "umbra/demux/byte_queue.h"
#include "umbra/demux/demuxer.h"
#include "umbra/demux/ebml.h"
#include "umbra/demux/mkv_elements.h"
#include "umbra/demux/mkv_seek_map.h"
#include "umbra/demux/mkv_tracks.h"

namespace umbra::demux {
namespace {

using Advance = absl::StatusOr<std::optional<StepResult>>;

constexpr uint64_t kUnbounded = kEbmlUnknownSize;
constexpr int64_t kScanEmitEveryPoints = 32;
constexpr int64_t kScanEmitFirstPoints = 8;

absl::Status Truncated() { return codec::Corrupt("the file is truncated"); }

Advance Continue() { return std::optional<StepResult>(); }

Advance Return(StepResult result) { return std::optional<StepResult>(result); }

penumbra::Rational NanosecondScaleBase(uint64_t scale) {
  return penumbra::Rational::Create(static_cast<int64_t>(scale), 1000000000)
      .value_or(penumbra::Rational::Of(1, 1000));
}

}

MatroskaDemuxer::MatroskaDemuxer(DemuxerOutput& output)
    : output_(output), scale_base_(NanosecondScaleBase(1000000)) {}

void MatroskaDemuxer::SetSourceInfo(std::optional<uint64_t> size,
                                    bool seekable) {
  source_size_ = size;
  seekable_ = seekable;
}

uint64_t MatroskaDemuxer::NextOffset() const { return queue_.EndOffset(); }

absl::Status MatroskaDemuxer::Append(uint64_t offset,
                                     penumbra::SharedBytes bytes) {
  if (offset != queue_.EndOffset()) {
    return absl::FailedPreconditionError(
        absl::StrCat("bytes appended at offset ", offset,
                     " but the demuxer expects ", queue_.EndOffset()));
  }
  queue_.Append(std::move(bytes));
  return absl::OkStatus();
}

void MatroskaDemuxer::EndOfInput() { ended_ = true; }

absl::StatusOr<StepResult> MatroskaDemuxer::Step() {
  if (!failure_.ok()) {
    return failure_;
  }
  if (finished_) {
    return StepResult::EndOfStream();
  }
  for (size_t unit = 0; unit < kMatroskaStepBudgetUnits; ++unit) {
    Advance advance = AdvanceElements();
    if (!advance.ok()) {
      failure_ = advance.status();
      return failure_;
    }
    if (advance->has_value()) {
      return **advance;
    }
    if (finished_) {
      return StepResult::EndOfStream();
    }
  }
  return StepResult::Yield();
}

void MatroskaDemuxer::AbandonCues() {
  fetching_cues_ = false;
  ended_ = false;
  next_element_ = resume_offset_;
}

MatroskaDemuxer::Advance MatroskaDemuxer::NeedMore(size_t wanted) {
  if (ended_) {
    if (fetching_cues_) {
      AbandonCues();
      return Continue();
    }
    return Truncated();
  }
  return Return(StepResult::NeedMoreData(queue_.EndOffset(),
                                         std::max<size_t>(wanted, 1)));
}

MatroskaDemuxer::Advance MatroskaDemuxer::MoveTo(uint64_t target) {
  const uint64_t start = queue_.StartOffset();
  const uint64_t end = queue_.EndOffset();
  if (target == start) {
    return Continue();
  }
  if (target > start && target <= end) {
    queue_.Discard(target - start);
    return Continue();
  }
  if (target > end && target - end <= kMatroskaMaxSkipBytes) {
    queue_.Discard(queue_.size());
    if (ended_) {
      if (fetching_cues_) {
        AbandonCues();
        return Continue();
      }
      return Truncated();
    }
    return Return(StepResult::NeedMoreData(end, target - end));
  }
  queue_.Reset(target);
  ended_ = false;
  return Return(StepResult::Seek(target));
}

MatroskaDemuxer::Advance MatroskaDemuxer::Malformed(const absl::Status& why) {
  if (fetching_cues_) {
    AbandonCues();
    return Continue();
  }
  if (!in_segment_) {
    return why;
  }
  ++stats_.resyncs;
  queue_.Discard(1);
  in_cluster_ = false;
  resyncing_ = true;
  resync_scanned_ = 0;
  next_element_ = queue_.StartOffset();
  return Continue();
}

MatroskaDemuxer::Advance MatroskaDemuxer::AdvanceResync() {
  const size_t window = std::min(queue_.size(), kMatroskaResyncWindowBytes);
  if (window < kMaxEbmlIdBytes) {
    if (!ended_) {
      return NeedMore(kMaxEbmlIdBytes - window);
    }
    queue_.Discard(queue_.size());
    next_element_ = queue_.StartOffset();
    resyncing_ = false;
    return FinishStream();
  }
  const std::vector<uint8_t> bytes = queue_.Peek(window);
  size_t found = bytes.size();
  for (size_t index = 0; index + kMaxEbmlIdBytes <= bytes.size(); ++index) {
    uint32_t id = 0;
    for (size_t offset = 0; offset < kMaxEbmlIdBytes; ++offset) {
      id = (id << 8) | bytes[index + offset];
    }
    if (IsSegmentChildId(id)) {
      found = index;
      break;
    }
  }
  if (found < bytes.size()) {
    queue_.Discard(found);
    next_element_ = queue_.StartOffset();
    resyncing_ = false;
    return Continue();
  }
  const size_t scanned = bytes.size() - (kMaxEbmlIdBytes - 1);
  resync_scanned_ += scanned;
  if (resync_scanned_ > kMatroskaMaxResyncBytes) {
    return codec::Corrupt("the file has too much unreadable data");
  }
  queue_.Discard(scanned);
  next_element_ = queue_.StartOffset();
  return Continue();
}

MatroskaDemuxer::Advance MatroskaDemuxer::AdvanceElements() {
  if (resyncing_) {
    return AdvanceResync();
  }
  if (in_cluster_ && cluster_end_ != kUnbounded &&
      next_element_ >= cluster_end_) {
    in_cluster_ = false;
  }
  if (in_segment_ && segment_end_ != kUnbounded &&
      next_element_ >= segment_end_) {
    in_segment_ = false;
    in_cluster_ = false;
  }
  ALUMBRA_ASSIGN_OR_RETURN(const std::optional<StepResult> moved,
                           MoveTo(next_element_));
  if (moved.has_value()) {
    return Return(*moved);
  }
  if (queue_.size() == 0) {
    if (!ended_) {
      return NeedMore(1);
    }
    if (fetching_cues_) {
      AbandonCues();
      return Continue();
    }
    return FinishStream();
  }
  const std::vector<uint8_t> prefix = queue_.Peek(kMaxEbmlHeaderBytes);
  const absl::StatusOr<size_t> needed = EbmlHeaderBytes(prefix);
  if (!needed.ok()) {
    return Malformed(needed.status());
  }
  if (prefix.size() < *needed) {
    return NeedMore(*needed - prefix.size());
  }
  const absl::StatusOr<EbmlHeader> header = ParseEbmlHeader(prefix);
  if (!header.ok()) {
    return Malformed(header.status());
  }
  return HandleElement(*header);
}

MatroskaDemuxer::Advance MatroskaDemuxer::FinishStream() {
  if (!announced_) {
    return codec::Corrupt("the file has no tracks");
  }
  if (scan_dirty_) {
    scan_dirty_ = false;
    std::vector<SeekPoint> points;
    points.reserve(scan_points_.size());
    for (const auto& [key, point] : scan_points_) {
      points.push_back(point);
    }
    EmitSeekMap(std::move(points));
  }
  finished_ = true;
  return Return(StepResult::EndOfStream());
}

absl::Status MatroskaDemuxer::CheckWithinParents(uint64_t end) const {
  if (in_cluster_ && cluster_end_ != kUnbounded && end > cluster_end_) {
    return codec::Corrupt("element extends beyond its cluster");
  }
  if (in_segment_ && segment_end_ != kUnbounded && end > segment_end_) {
    return codec::Corrupt("element extends beyond its segment");
  }
  return absl::OkStatus();
}

MatroskaDemuxer::Advance MatroskaDemuxer::HandleElement(
    const EbmlHeader& header) {
  const uint64_t start = queue_.StartOffset();
  const uint32_t id = header.id;
  if (id == kIdEbml) {
    return Buffered(header, start, kMaxHeaderElementBytes,
                    &MatroskaDemuxer::OnEbmlHeader);
  }
  if (!header_seen_) {
    return codec::Corrupt("the file does not start with an EBML header");
  }
  if (id == kIdSegment) {
    return EnterSegment(header, start);
  }
  if (!in_segment_) {
    if (!announced_ || !IsSegmentChildId(id)) {
      return SkipElement(header, start);
    }
    in_segment_ = true;
    segment_end_ = kUnbounded;
  }
  if (in_cluster_) {
    if (!IsLevelOneId(id)) {
      return HandleClusterChild(header, start);
    }
    in_cluster_ = false;
  }
  return HandleSegmentChild(header, start);
}

MatroskaDemuxer::Advance MatroskaDemuxer::EnterSegment(const EbmlHeader& header,
                                                       uint64_t start) {
  if (!header.UnknownSize() &&
      header.size > kUnbounded - start - header.header_size) {
    return codec::Corrupt("segment size overflows the file offset");
  }
  in_segment_ = true;
  in_cluster_ = false;
  segment_data_start_ = start + header.header_size;
  segment_end_ =
      header.UnknownSize() ? kUnbounded : segment_data_start_ + header.size;
  next_element_ = segment_data_start_;
  return Continue();
}

MatroskaDemuxer::Advance MatroskaDemuxer::HandleSegmentChild(
    const EbmlHeader& header, uint64_t start) {
  const uint32_t id = header.id;
  if (fetching_cues_ && id != kIdCues) {
    AbandonCues();
    return Continue();
  }
  if (id == kIdCluster) {
    return EnterCluster(header, start);
  }
  if (id == kIdSeekHead) {
    return Buffered(header, start, kMaxSeekHeadBytes,
                    &MatroskaDemuxer::OnSeekHead);
  }
  if (id == kIdInfo) {
    return Buffered(header, start, kMaxInfoBytes, &MatroskaDemuxer::OnInfo);
  }
  if (id == kIdTracks) {
    return Buffered(header, start, kMaxTracksBytes, &MatroskaDemuxer::OnTracks);
  }
  if (id == kIdCues) {
    return Buffered(header, start, kMaxCuesBytes, &MatroskaDemuxer::OnCues);
  }
  return SkipElement(header, start);
}

MatroskaDemuxer::Advance MatroskaDemuxer::HandleClusterChild(
    const EbmlHeader& header, uint64_t start) {
  const uint32_t id = header.id;
  if (id == kIdClusterTimestamp) {
    return Buffered(header, start, kMaxHeaderElementBytes,
                    &MatroskaDemuxer::OnClusterTimestamp);
  }
  if (id == kIdSimpleBlock) {
    return Buffered(header, start, kMaxBlockElementBytes,
                    &MatroskaDemuxer::OnSimpleBlock);
  }
  if (id == kIdBlockGroup) {
    return Buffered(header, start, kMaxBlockElementBytes,
                    &MatroskaDemuxer::OnBlockGroup);
  }
  return SkipElement(header, start);
}

MatroskaDemuxer::Advance MatroskaDemuxer::EnterCluster(const EbmlHeader& header,
                                                       uint64_t start) {
  if (tracks_pending_) {
    ALUMBRA_RETURN_IF_ERROR(Announce());
  } else if (!announced_) {
    return codec::Corrupt("a cluster appears before the tracks");
  }
  if (!header.UnknownSize() &&
      header.size > kUnbounded - start - header.header_size) {
    return Malformed(codec::Corrupt("cluster size overflows the file offset"));
  }
  const uint64_t end = header.UnknownSize()
                           ? kUnbounded
                           : start + header.header_size + header.size;
  if (end != kUnbounded) {
    const absl::Status within = CheckWithinParents(end);
    if (!within.ok()) {
      return Malformed(within);
    }
  }
  if (!cues_seen_ && !cues_attempted_ && cues_position_.has_value() &&
      seekable_) {
    cues_attempted_ = true;
    if (*cues_position_ <= kUnbounded - segment_data_start_ &&
        segment_data_start_ + *cues_position_ > start) {
      fetching_cues_ = true;
      resume_offset_ = start;
      next_element_ = segment_data_start_ + *cues_position_;
      return Continue();
    }
  }
  in_cluster_ = true;
  cluster_start_ = start;
  cluster_end_ = end;
  cluster_ticks_.reset();
  next_element_ = start + header.header_size;
  return Continue();
}

MatroskaDemuxer::Advance MatroskaDemuxer::SkipElement(const EbmlHeader& header,
                                                      uint64_t start) {
  if (header.UnknownSize()) {
    return Malformed(
        codec::Corrupt("element of unexpected type has an unknown size"));
  }
  if (header.size > kUnbounded - start - header.header_size) {
    return Malformed(codec::Corrupt("element size overflows the file offset"));
  }
  const uint64_t end = start + header.header_size + header.size;
  const absl::Status within = CheckWithinParents(end);
  if (!within.ok()) {
    return Malformed(within);
  }
  next_element_ = end;
  return Continue();
}

MatroskaDemuxer::Advance MatroskaDemuxer::Buffered(const EbmlHeader& header,
                                                   uint64_t start,
                                                   uint64_t limit,
                                                   Handler handler) {
  if (header.UnknownSize()) {
    return Malformed(
        codec::Corrupt("element of a fixed type has an unknown size"));
  }
  if (header.size > limit) {
    return Malformed(codec::Corrupt(absl::StrCat(
        "element of ", header.size, " bytes exceeds the buffering limit")));
  }
  const uint64_t total = header.header_size + header.size;
  const absl::Status within = CheckWithinParents(start + total);
  if (!within.ok()) {
    return Malformed(within);
  }
  if (queue_.size() < total) {
    return NeedMore(static_cast<size_t>(total) - queue_.size());
  }
  const std::optional<penumbra::SharedBytes> element =
      queue_.Read(static_cast<size_t>(total));
  if (!element.has_value()) {
    return Truncated();
  }
  next_element_ = start + total;
  ALUMBRA_RETURN_IF_ERROR((this->*handler)(*element, header.header_size));
  return Continue();
}

absl::Status MatroskaDemuxer::OnEbmlHeader(const penumbra::SharedBytes& element,
                                           size_t header_size) {
  ALUMBRA_RETURN_IF_ERROR(
      ParseEbmlHeaderElement(element.span().subspan(header_size)));
  header_seen_ = true;
  in_segment_ = false;
  in_cluster_ = false;
  ResetSegmentState();
  return absl::OkStatus();
}

absl::Status MatroskaDemuxer::OnSeekHead(const penumbra::SharedBytes& element,
                                         size_t header_size) {
  ALUMBRA_ASSIGN_OR_RETURN(const MkvSeekHead head,
                           ParseSeekHead(element.span().subspan(header_size)));
  if (!cues_position_.has_value()) {
    cues_position_ = head.cues_position;
  }
  return absl::OkStatus();
}

absl::Status MatroskaDemuxer::OnInfo(const penumbra::SharedBytes& element,
                                     size_t header_size) {
  ALUMBRA_ASSIGN_OR_RETURN(info_,
                           ParseInfo(element.span().subspan(header_size)));
  info_seen_ = true;
  if (tracks_pending_) {
    return Announce();
  }
  return absl::OkStatus();
}

absl::Status MatroskaDemuxer::OnTracks(const penumbra::SharedBytes& element,
                                       size_t header_size) {
  ALUMBRA_ASSIGN_OR_RETURN(raw_tracks_,
                           ParseTracks(element.span().subspan(header_size)));
  tracks_pending_ = true;
  if (info_seen_) {
    return Announce();
  }
  return absl::OkStatus();
}

absl::Status MatroskaDemuxer::OnCues(const penumbra::SharedBytes& element,
                                     size_t header_size) {
  ALUMBRA_ASSIGN_OR_RETURN(cue_points_,
                           ParseCues(element.span().subspan(header_size)));
  cues_seen_ = true;
  scan_points_.clear();
  scan_dirty_ = false;
  if (fetching_cues_) {
    fetching_cues_ = false;
    next_element_ = resume_offset_;
  }
  if (announced_ && !tracks_pending_) {
    EmitCueMap();
  }
  return absl::OkStatus();
}

absl::Status MatroskaDemuxer::OnClusterTimestamp(
    const penumbra::SharedBytes& element, size_t header_size) {
  ALUMBRA_ASSIGN_OR_RETURN(
      const uint64_t ticks,
      ReadEbmlUnsigned(element.span().subspan(header_size)));
  if (ticks > kMaxMkvTimeline) {
    return codec::Corrupt("cluster timestamp is out of range");
  }
  if (last_cluster_ticks_.has_value() && ticks < *last_cluster_ticks_) {
    for (Track& track : tracks_) {
      track.discontinuity_pending = true;
    }
  }
  last_cluster_ticks_ = ticks;
  cluster_ticks_ = ticks;
  if (!cues_seen_) {
    AddScanPoint(ticks, cluster_start_);
  }
  return absl::OkStatus();
}

void MatroskaDemuxer::AddScanPoint(uint64_t ticks, uint64_t offset) {
  if (scan_points_.size() >= kMaxScanPoints) {
    return;
  }
  const std::optional<int64_t> micros = penumbra::Rescale(
      static_cast<int64_t>(ticks), scale_base_, penumbra::kMicrosecondTimeBase,
      penumbra::Rounding::kNearest);
  if (!micros.has_value()) {
    return;
  }
  const bool inserted =
      scan_points_
          .emplace(
              *micros,
              SeekPoint{.time = penumbra::MediaTime::FromMicroseconds(*micros),
                        .byte_offset = offset})
          .second;
  if (!inserted) {
    return;
  }
  scan_dirty_ = true;
  const auto count = static_cast<int64_t>(scan_points_.size());
  if (count <= kScanEmitFirstPoints || count % kScanEmitEveryPoints == 0) {
    scan_dirty_ = false;
    std::vector<SeekPoint> points;
    points.reserve(scan_points_.size());
    for (const auto& [key, point] : scan_points_) {
      points.push_back(point);
    }
    EmitSeekMap(std::move(points));
  }
}

void MatroskaDemuxer::EmitSeekMap(std::vector<SeekPoint> points) {
  int64_t pre_roll = 0;
  for (const Track& track : tracks_) {
    pre_roll = std::max(pre_roll, track.config.seek_pre_roll_micros);
  }
  output_.OnSeekMap(std::make_shared<MatroskaSeekMap>(
      std::move(points), duration_,
      penumbra::TimeDelta::FromMicroseconds(pre_roll)));
}

void MatroskaDemuxer::EmitCueMap() {
  uint64_t reference = tracks_.empty() ? 0 : tracks_.front().config.number;
  for (const Track& track : tracks_) {
    if (track.video) {
      reference = track.config.number;
      break;
    }
  }
  const bool has_reference = std::ranges::any_of(
      cue_points_,
      [&](const MkvCuePoint& cue) { return cue.track == reference; });
  std::map<int64_t, SeekPoint> unique;
  for (const MkvCuePoint& cue : cue_points_) {
    if ((has_reference && cue.track != reference) ||
        cue.time > kMaxMkvTimeline ||
        cue.cluster_position > kUnbounded - segment_data_start_) {
      continue;
    }
    const std::optional<int64_t> micros = penumbra::Rescale(
        static_cast<int64_t>(cue.time), scale_base_,
        penumbra::kMicrosecondTimeBase, penumbra::Rounding::kNearest);
    if (!micros.has_value()) {
      continue;
    }
    unique.emplace(
        *micros,
        SeekPoint{.time = penumbra::MediaTime::FromMicroseconds(*micros),
                  .byte_offset = segment_data_start_ + cue.cluster_position});
  }
  std::vector<SeekPoint> points;
  points.reserve(unique.size());
  for (const auto& [key, point] : unique) {
    points.push_back(point);
  }
  EmitSeekMap(std::move(points));
}

absl::Status MatroskaDemuxer::Announce() {
  const std::optional<penumbra::Rational> base = penumbra::Rational::Create(
      static_cast<int64_t>(info_.timestamp_scale), 1000000000);
  if (!base.has_value()) {
    return codec::Corrupt("timestamp scale is out of range");
  }
  std::vector<Track> built;
  size_t skipped = 0;
  for (const MkvTrack& raw : raw_tracks_) {
    ALUMBRA_ASSIGN_OR_RETURN(std::optional<MkvTrackConfig> config,
                             BuildTrackConfig(raw, *base));
    if (!config.has_value()) {
      ++skipped;
      continue;
    }
    Track track;
    track.video =
        std::holds_alternative<penumbra::VideoInfo>(config->stream.format);
    track.config = *std::move(config);
    track.config_change_pending = announced_;
    built.push_back(std::move(track));
  }
  if (built.empty()) {
    return codec::Unsupported("the file has no supported track");
  }
  stats_.skipped_tracks += skipped;
  scale_base_ = *base;
  tracks_ = std::move(built);
  announced_ = true;
  tracks_pending_ = false;
  duration_ = penumbra::TimeDelta::Unknown();
  if (info_.duration_ticks.has_value()) {
    const std::optional<int64_t> micros = penumbra::Rescale(
        static_cast<int64_t>(std::llround(*info_.duration_ticks)), scale_base_,
        penumbra::kMicrosecondTimeBase, penumbra::Rounding::kNearest);
    if (micros.has_value()) {
      duration_ = penumbra::TimeDelta::FromMicroseconds(*micros);
    }
  }
  std::vector<penumbra::StreamInfo> streams;
  streams.reserve(tracks_.size());
  for (const Track& track : tracks_) {
    streams.push_back(track.config.stream);
  }
  output_.OnStreams(std::move(streams));
  EmitSeekMap({});
  if (cues_seen_) {
    EmitCueMap();
  }
  return absl::OkStatus();
}

MatroskaDemuxer::Track* MatroskaDemuxer::FindTrack(uint64_t number) {
  for (Track& track : tracks_) {
    if (track.config.number == number) {
      return &track;
    }
  }
  return nullptr;
}

void MatroskaDemuxer::ResetSegmentState() {
  info_ = MkvInfo();
  info_seen_ = false;
  raw_tracks_.clear();
  tracks_pending_ = false;
  cues_position_.reset();
  cue_points_.clear();
  cues_seen_ = false;
  cues_attempted_ = false;
  fetching_cues_ = false;
  scan_points_.clear();
  scan_dirty_ = false;
  segment_data_start_ = 0;
  segment_end_ = kUnbounded;
  cluster_ticks_.reset();
  last_cluster_ticks_.reset();
}

void MatroskaDemuxer::ResetState(uint64_t offset) {
  queue_.Reset(offset);
  next_element_ = offset;
  ended_ = false;
  finished_ = false;
  failure_ = absl::OkStatus();
  in_cluster_ = false;
  fetching_cues_ = false;
  resyncing_ = false;
  cluster_ticks_.reset();
  last_cluster_ticks_.reset();
  for (Track& track : tracks_) {
    track.discontinuity_pending = true;
  }
}

void MatroskaDemuxer::Reset(uint64_t offset) {
  ResetState(offset);
  in_segment_ = false;
  segment_end_ = kUnbounded;
}

absl::StatusOr<uint64_t> MatroskaDemuxer::Seek(const SeekPoint& point) {
  if (!announced_) {
    return absl::FailedPreconditionError("seek before the tracks are parsed");
  }
  ResetState(point.byte_offset);
  in_segment_ = true;
  return queue_.StartOffset();
}

}
