#include "umbra/demux/ts_demuxer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/base/status.h"
#include "umbra/codec/nal_unit.h"
#include "umbra/demux/caption_extractor.h"
#include "umbra/demux/demuxer.h"
#include "umbra/demux/ts_framing.h"
#include "umbra/demux/ts_packet.h"
#include "umbra/demux/ts_seek_map.h"

namespace umbra::demux {
namespace {

constexpr size_t kLockPackets = 5;
constexpr size_t kMaxUnit = 204;
constexpr size_t kScanBytes = 4096;
constexpr std::array<size_t, 3> kUnits = {188, 192, 204};

struct SyncLock {
  size_t start = 0;
  size_t unit = 0;
};

bool LocksAt(std::span<const uint8_t> window, size_t start, size_t unit) {
  if (start + unit > window.size()) {
    return false;
  }
  for (size_t index = 1; index < kLockPackets; ++index) {
    const size_t position = start + (index * unit);
    if (position < window.size() && window[position] != ts::kSyncByte) {
      return false;
    }
  }
  return true;
}

std::optional<SyncLock> FindSync(std::span<const uint8_t> window,
                                 size_t limit) {
  for (size_t start = 0; start < limit; ++start) {
    if (window[start] != ts::kSyncByte) {
      continue;
    }
    for (const size_t unit : kUnits) {
      if (LocksAt(window, start, unit)) {
        return SyncLock{.start = start, .unit = unit};
      }
    }
  }
  return std::nullopt;
}

bool IsVideo(ts::EsKind kind) {
  return kind == ts::EsKind::kH264 || kind == ts::EsKind::kHevc;
}

bool IsMedia(ts::EsKind kind) { return kind != ts::EsKind::kId3; }

penumbra::Timestamp Ticks(int64_t value) {
  return penumbra::Timestamp{.value = value, .time_base = ts::kTimeBase};
}

int64_t SampleTicks(int64_t samples, int32_t rate) {
  const std::optional<penumbra::Rational> base =
      penumbra::Rational::Create(1, rate);
  if (!base.has_value()) {
    return 0;
  }
  return penumbra::Rescale(samples, *base, ts::kTimeBase,
                           penumbra::Rounding::kNearest)
      .value_or(0);
}

bool SameConfig(const penumbra::StreamInfo& a, const penumbra::StreamInfo& b) {
  return a.codec == b.codec && a.codec_string == b.codec_string &&
         a.nal_unit_framing == b.nal_unit_framing &&
         a.nal_length_size == b.nal_length_size &&
         a.aac_framing == b.aac_framing &&
         std::ranges::equal(a.codec_config.span(), b.codec_config.span());
}

}

TsDemuxer::TsDemuxer(DemuxerOutput& output, TsDemuxerOptions options)
    : output_(output), options_(options) {
  last_continuity_.fill(-1);
  stream_index_.fill(-1);
}

void TsDemuxer::SetSourceInfo(std::optional<uint64_t> size, bool seekable) {
  source_size_ = size;
  seekable_ = seekable;
}

uint64_t TsDemuxer::NextOffset() const { return queue_.EndOffset(); }

absl::Status TsDemuxer::Append(uint64_t offset, penumbra::SharedBytes bytes) {
  if (offset != queue_.EndOffset()) {
    return absl::FailedPreconditionError(
        "the appended bytes do not start at the next offset");
  }
  queue_.Append(std::move(bytes));
  return absl::OkStatus();
}

void TsDemuxer::EndOfInput() { ended_ = true; }

bool TsDemuxer::AtEnd() const {
  return ended_ ||
         (source_size_.has_value() && queue_.EndOffset() >= *source_size_);
}

void TsDemuxer::RecordFailure(const absl::Status& status) {
  if (failure_.ok()) {
    failure_ = status;
  }
}

absl::StatusOr<StepResult> TsDemuxer::Step() {
  if (!failure_.ok()) {
    return failure_;
  }
  if (finished_) {
    return StepResult::EndOfStream();
  }
  for (size_t budget = 0; budget < kTsStepBudgetPackets; ++budget) {
    Advance advance = AdvancePacket();
    if (!advance.ok()) {
      RecordFailure(advance.status());
    }
    if (!failure_.ok()) {
      return failure_;
    }
    if (advance->has_value()) {
      return **advance;
    }
  }
  return StepResult::Yield();
}

TsDemuxer::Advance TsDemuxer::NeedMore(size_t wanted) const {
  return StepResult::NeedMoreData(queue_.EndOffset(),
                                  std::max<size_t>(wanted, 1));
}

TsDemuxer::Advance TsDemuxer::TakeRequest() {
  if (!seek_request_.has_value()) {
    return std::optional<StepResult>();
  }
  const uint64_t offset = *seek_request_;
  seek_request_.reset();
  MoveTo(offset);
  return std::optional<StepResult>(StepResult::Seek(offset));
}

void TsDemuxer::MoveTo(uint64_t offset) {
  queue_.Reset(offset);
  ended_ = false;
  finished_ = false;
  locked_ = false;
  last_continuity_.fill(-1);
  pat_assembler_.Reset();
  pmt_assembler_.Reset();
  DropAllPartial();
}

uint64_t TsDemuxer::AlignDown(uint64_t offset) const {
  if (offset <= sync_base_) {
    return sync_base_;
  }
  return sync_base_ + (((offset - sync_base_) / packet_size_) * packet_size_);
}

void TsDemuxer::DropAllPartial() {
  for (Stream& stream : streams_) {
    stream.pes_active = false;
    stream.pes.clear();
  }
}

void TsDemuxer::MarkAllDiscontinuous() {
  for (Stream& stream : streams_) {
    stream.discontinuity_pending = true;
  }
}

void TsDemuxer::LoseSync() {
  ++stats_.sync_losses;
  locked_ = false;
  last_continuity_.fill(-1);
  pat_assembler_.Reset();
  pmt_assembler_.Reset();
  DropAllPartial();
  MarkAllDiscontinuous();
}

TsDemuxer::Advance TsDemuxer::AcquireSync() {
  const bool at_end = AtEnd();
  if (queue_.size() < kScanBytes && !at_end) {
    return NeedMore(kScanBytes - queue_.size());
  }
  const std::vector<uint8_t> window =
      queue_.Peek(std::min(queue_.size(), kScanBytes));
  const size_t verifiable = (kLockPackets - 1) * kMaxUnit;
  size_t limit = window.size();
  if (!at_end) {
    limit = window.size() > verifiable ? window.size() - verifiable : 0;
  }
  const std::optional<SyncLock> lock = FindSync(window, limit);
  if (lock.has_value()) {
    if (!ever_locked_) {
      sync_base_ = queue_.StartOffset() + lock->start;
      ever_locked_ = true;
    }
    queue_.Discard(lock->start);
    packet_size_ = lock->unit;
    locked_ = true;
    return std::optional<StepResult>();
  }
  queue_.Discard(limit);
  if (at_end) {
    queue_.Discard(queue_.size());
    if (!ever_locked_) {
      return absl::DataLossError("the input has no MPEG-TS packets");
    }
    return FinishInput();
  }
  return std::optional<StepResult>();
}

TsDemuxer::Advance TsDemuxer::AdvancePacket() {
  if (!locked_) {
    return AcquireSync();
  }
  const size_t needed = AtEnd() ? ts::kPacketSize : packet_size_;
  if (queue_.size() < needed) {
    if (!AtEnd()) {
      return NeedMore(
          std::max(packet_size_ - queue_.size(), kTsPreferredReadBytes));
    }
    return FinishInput();
  }
  const size_t unit = std::min(packet_size_, queue_.size());
  const std::vector<uint8_t> bytes = queue_.Peek(unit);
  if (bytes.front() != ts::kSyncByte) {
    LoseSync();
    queue_.Discard(1);
    return std::optional<StepResult>();
  }
  const uint64_t offset = queue_.StartOffset();
  queue_.Discard(unit);
  if (mode_ == Mode::kStream && !announced_ && pmt_seen_) {
    ++probe_packets_;
  }
  ALUMBRA_RETURN_IF_ERROR(HandlePacket(
      std::span<const uint8_t>(bytes).first(ts::kPacketSize), offset));
  if (!failure_.ok()) {
    return failure_;
  }
  if (!announced_ && pmt_seen_ && probe_packets_ >= kTsProbePackets) {
    ALUMBRA_RETURN_IF_ERROR(TryAnnounce(false));
  }
  if (mode_ == Mode::kBisect &&
      (bisect_.found.has_value() || queue_.StartOffset() >= bisect_.high)) {
    EndProbe();
  }
  return TakeRequest();
}

TsDemuxer::Advance TsDemuxer::FinishInput() {
  queue_.Discard(queue_.size());
  if (mode_ == Mode::kTail) {
    for (Stream& stream : streams_) {
      if (stream.pes_active) {
        CompleteOrDrop(stream);
      }
    }
    FinishTail();
    return TakeRequest();
  }
  if (mode_ == Mode::kBisect) {
    EndProbe();
    return TakeRequest();
  }
  for (Stream& stream : streams_) {
    if (stream.pes_active) {
      CompleteOrDrop(stream);
    }
  }
  for (Stream& stream : streams_) {
    FlushHeld(stream);
  }
  ALUMBRA_RETURN_IF_ERROR(TryAnnounce(true));
  if (!failure_.ok()) {
    return failure_;
  }
  if (map_deferred_ && !map_emitted_) {
    EmitSeekMap(true);
  }
  finished_ = true;
  return std::optional<StepResult>(StepResult::EndOfStream());
}

TsDemuxer::Continuity TsDemuxer::CheckContinuity(
    const ts::PacketHeader& header) {
  if (!header.has_payload) {
    return Continuity::kOk;
  }
  int16_t& last = last_continuity_.at(header.pid);
  const int16_t current = header.continuity;
  Continuity result = Continuity::kOk;
  if (last >= 0) {
    if (current == last) {
      result = Continuity::kDuplicate;
    } else if (current != ((last + 1) & 15)) {
      result = Continuity::kLost;
    }
  }
  if (header.adaptation.discontinuity && result == Continuity::kLost) {
    result = Continuity::kOk;
  }
  last = current;
  return result;
}

int TsDemuxer::StreamIndex(uint16_t pid) const { return stream_index_.at(pid); }

void TsDemuxer::RebuildIndex() {
  stream_index_.fill(-1);
  for (size_t index = 0; index < streams_.size(); ++index) {
    stream_index_.at(streams_[index].pmt.pid) = static_cast<int16_t>(index);
  }
}

absl::Status TsDemuxer::HandlePacket(std::span<const uint8_t> packet,
                                     uint64_t offset) {
  const std::optional<ts::PacketHeader> parsed = ts::ParsePacket(packet);
  if (!parsed.has_value()) {
    ++stats_.corrupt_packets;
    return absl::OkStatus();
  }
  const ts::PacketHeader& header = *parsed;
  if (header.pid == ts::kNullPid) {
    return absl::OkStatus();
  }
  const int index = StreamIndex(header.pid);
  const bool is_pat = header.pid == ts::kPatPid;
  const bool is_pmt = pmt_pid_.has_value() && header.pid == *pmt_pid_;
  if (!is_pat && !is_pmt && index < 0 && header.pid != pcr_pid_) {
    return absl::OkStatus();
  }
  Stream* stream =
      index >= 0 ? &streams_.at(static_cast<size_t>(index)) : nullptr;
  if (!AdmitPacket(header, stream, is_pat, is_pmt) || !header.has_payload) {
    return absl::OkStatus();
  }
  if (is_pat) {
    HandlePsi(header, pat_assembler_, true);
  } else if (is_pmt) {
    HandlePsi(header, pmt_assembler_, false);
  } else if (stream != nullptr) {
    HandleEsPacket(*stream, header, offset);
  }
  return failure_;
}

void TsDemuxer::DropPartial(Stream& stream) {
  stream.pes_active = false;
  stream.pes.clear();
  stream.discontinuity_pending = true;
}

bool TsDemuxer::AdmitPacket(const ts::PacketHeader& header, Stream* stream,
                            bool is_pat, bool is_pmt) {
  if (header.transport_error) {
    ++stats_.transport_errors;
    last_continuity_.at(header.pid) = -1;
    if (stream != nullptr) {
      DropPartial(*stream);
    }
    pat_assembler_.Reset();
    pmt_assembler_.Reset();
    return false;
  }
  if (header.scrambling != 0) {
    ++stats_.scrambled_packets;
    return false;
  }
  if (header.adaptation.pcr_27mhz.has_value()) {
    ++stats_.pcr_count;
  }
  if (header.adaptation.discontinuity) {
    ApplyDiscontinuityIndicator(header, stream);
  }
  const Continuity continuity = CheckContinuity(header);
  if (continuity == Continuity::kDuplicate) {
    ++stats_.duplicate_packets;
    return false;
  }
  if (continuity == Continuity::kLost) {
    ++stats_.continuity_errors;
    if (stream != nullptr) {
      DropPartial(*stream);
    }
    if (is_pat) {
      pat_assembler_.Reset();
    }
    if (is_pmt) {
      pmt_assembler_.Reset();
    }
  }
  return true;
}

void TsDemuxer::ApplyDiscontinuityIndicator(const ts::PacketHeader& header,
                                            Stream* stream) {
  if (stream != nullptr && header.payload_unit_start && stream->pes_active) {
    CompleteOrDrop(*stream);
  }
  if (header.pid == pcr_pid_) {
    MarkAllDiscontinuous();
  } else if (stream != nullptr) {
    stream->discontinuity_pending = true;
  }
}

void TsDemuxer::HandlePsi(const ts::PacketHeader& header,
                          ts::SectionAssembler& assembler, bool is_pat) {
  std::vector<ts::Section> sections;
  assembler.Feed(header.payload, header.payload_unit_start, sections);
  for (const ts::Section& section : sections) {
    if (!section.crc_ok) {
      ++stats_.crc_errors;
      MarkAllDiscontinuous();
      continue;
    }
    if (is_pat) {
      const std::optional<ts::Pat> pat = ts::ParsePat(section.bytes);
      if (pat.has_value()) {
        OnPat(*pat);
      }
    } else {
      const std::optional<ts::Pmt> pmt = ts::ParsePmt(section.bytes);
      if (pmt.has_value()) {
        OnPmt(*pmt);
      }
    }
  }
}

void TsDemuxer::OnPat(const ts::Pat& pat) {
  if (mode_ != Mode::kStream && announced_) {
    return;
  }
  if (!pat_version_.has_value() || *pat_version_ != pat.version) {
    programs_.clear();
    pat_version_ = pat.version;
  }
  for (const ts::PatEntry& entry : pat.programs) {
    const bool known =
        std::ranges::any_of(programs_, [&entry](const ts::PatEntry& existing) {
          return existing.program_number == entry.program_number;
        });
    if (!known) {
      programs_.push_back(entry);
    }
  }
  for (const ts::PatEntry& entry : programs_) {
    if (entry.program_number == 0) {
      continue;
    }
    if (options_.program_number != 0 &&
        entry.program_number != options_.program_number) {
      continue;
    }
    if (!pmt_pid_.has_value() || *pmt_pid_ != entry.pid ||
        !selected_program_.has_value() ||
        *selected_program_ != entry.program_number) {
      pmt_pid_ = entry.pid;
      selected_program_ = entry.program_number;
      pmt_assembler_.Reset();
      last_continuity_.at(entry.pid) = -1;
    }
    return;
  }
}

std::optional<ts::EsKind> TsDemuxer::Classify(const ts::PmtStream& stream) {
  switch (stream.stream_type) {
    case 0x1B:
      return ts::EsKind::kH264;
    case 0x24:
      return ts::EsKind::kHevc;
    case 0x0F:
      return ts::EsKind::kAdts;
    case 0x03:
    case 0x04:
      return ts::EsKind::kMpegAudio;
    case 0x81:
      return ts::EsKind::kAc3;
    case 0x87:
      return ts::EsKind::kEac3;
    case 0x15:
      return ts::EsKind::kId3;
    case 0x06:
      if (stream.descriptors.eac3) {
        return ts::EsKind::kEac3;
      }
      if (stream.descriptors.ac3) {
        return ts::EsKind::kAc3;
      }
      return std::nullopt;
    default:
      return std::nullopt;
  }
}

void TsDemuxer::OnPmt(const ts::Pmt& pmt) {
  if (mode_ != Mode::kStream || !selected_program_.has_value() ||
      pmt.program_number != *selected_program_) {
    return;
  }
  pmt_seen_ = true;
  pcr_pid_ = pmt.pcr_pid;
  std::vector<Stream> next;
  size_t skipped = 0;
  for (const ts::PmtStream& entry : pmt.streams) {
    const bool duplicate = std::ranges::any_of(
        next,
        [&entry](const Stream& stream) { return stream.pmt.pid == entry.pid; });
    const std::optional<ts::EsKind> kind = Classify(entry);
    if (duplicate || entry.pid == ts::kPatPid || entry.pid == ts::kNullPid) {
      continue;
    }
    if (!kind.has_value()) {
      ++skipped;
      continue;
    }
    Stream stream;
    stream.pmt = entry;
    stream.kind = *kind;
    next.push_back(std::move(stream));
  }
  const bool same =
      next.size() == streams_.size() &&
      std::ranges::equal(next, streams_, [](const Stream& a, const Stream& b) {
        return a.pmt == b.pmt && a.kind == b.kind;
      });
  if (same) {
    return;
  }
  const bool changed = !streams_.empty();
  if (changed) {
    ++stats_.pmt_changes;
  }
  stats_.skipped_streams += skipped;
  for (Stream& old : streams_) {
    if (old.pes_active) {
      CompleteOrDrop(old);
    }
    FlushHeld(old);
  }
  for (Stream& stream : next) {
    for (Stream& old : streams_) {
      if (old.pmt.pid == stream.pmt.pid && old.kind == stream.kind) {
        const ts::PmtStream replacement = stream.pmt;
        stream = std::move(old);
        stream.pmt = replacement;
        stream.pes_active = false;
        stream.pes.clear();
        stream.discontinuity_pending = true;
        stream.config_change_pending = stream.info.has_value();
        break;
      }
    }
  }
  streams_ = std::move(next);
  RebuildIndex();
  probe_packets_ = 0;
  if (changed) {
    announced_ = false;
  }
  const absl::Status status = TryAnnounce(false);
  if (!status.ok()) {
    RecordFailure(status);
  }
}

void TsDemuxer::HandleEsPacket(Stream& stream, const ts::PacketHeader& header,
                               uint64_t offset) {
  if (stream.dropped) {
    return;
  }
  if (header.payload_unit_start) {
    if (stream.pes_active) {
      CompleteOrDrop(stream);
    }
    stream.pes.clear();
    stream.pes_expected = 0;
    stream.pes_active = true;
    stream.pes_offset = offset;
    stream.pes_random_access = header.adaptation.random_access;
  } else if (!stream.pes_active) {
    return;
  }
  stream.pes.insert(stream.pes.end(), header.payload.begin(),
                    header.payload.end());
  if (stream.pes.size() > kTsMaxPesBytes) {
    stream.pes_active = false;
    stream.pes.clear();
    stream.discontinuity_pending = true;
    ++stats_.dropped_pes;
    return;
  }
  if (stream.pes.size() < 6) {
    return;
  }
  if (stream.pes[0] != 0 || stream.pes[1] != 0 || stream.pes[2] != 1) {
    stream.pes_active = false;
    stream.pes.clear();
    stream.discontinuity_pending = true;
    ++stats_.dropped_pes;
    return;
  }
  const size_t length =
      (static_cast<size_t>(stream.pes[4]) << 8U) | stream.pes[5];
  stream.pes_expected = length == 0 ? 0 : 6 + length;
  if (stream.pes_expected != 0 && stream.pes.size() >= stream.pes_expected) {
    CompletePes(stream);
  }
}

void TsDemuxer::CompleteOrDrop(Stream& stream) {
  if (stream.pes_expected != 0 && stream.pes.size() < stream.pes_expected) {
    stream.pes_active = false;
    stream.pes.clear();
    stream.discontinuity_pending = true;
    ++stats_.dropped_pes;
    return;
  }
  CompletePes(stream);
}

void TsDemuxer::CompletePes(Stream& stream) {
  std::vector<uint8_t> buffer = std::move(stream.pes);
  stream.pes.clear();
  stream.pes_active = false;
  if (mode_ == Mode::kStream) {
    HandlePes(stream, buffer, stream.pes_random_access);
  } else {
    ProbePes(stream, buffer, stream.pes_offset, stream.pes_random_access);
  }
}

void TsDemuxer::NoteTimes(int64_t first, int64_t pts) {
  if (!opened_ && (!first_ticks_.has_value() || first < *first_ticks_)) {
    first_ticks_ = first;
  }
  if (!max_ticks_.has_value() || pts > *max_ticks_) {
    max_ticks_ = pts;
  }
}

void TsDemuxer::HandlePes(Stream& stream, std::span<const uint8_t> pes,
                          bool random_access) {
  const std::optional<ts::PesHeader> header = ts::ParsePesHeader(pes);
  if (!header.has_value()) {
    ++stats_.dropped_pes;
    stream.discontinuity_pending = true;
    return;
  }
  size_t end = pes.size();
  if (header->packet_length != 0) {
    end = std::min(end, static_cast<size_t>(header->packet_length) + 6);
  }
  if (end < header->payload_offset ||
      !ts::StreamIdHasPesHeader(header->stream_id)) {
    return;
  }
  const std::span<const uint8_t> payload =
      pes.subspan(header->payload_offset, end - header->payload_offset);
  std::optional<int64_t> dts;
  std::optional<int64_t> pts;
  if (header->dts.has_value()) {
    dts = unwrapper_.Unwrap(*header->dts);
  }
  if (header->pts.has_value()) {
    pts = unwrapper_.Unwrap(*header->pts);
  }
  if (IsMedia(stream.kind) && pts.has_value()) {
    NoteTimes(dts.value_or(*pts), *pts);
  }
  if (IsVideo(stream.kind)) {
    HandleVideo(stream, payload, pts, dts, random_access);
  } else if (stream.kind == ts::EsKind::kId3) {
    HandleId3(stream, payload, pts);
  } else {
    HandleAudio(stream, payload, pts);
  }
  if (!announced_) {
    const absl::Status status = TryAnnounce(false);
    if (!status.ok()) {
      RecordFailure(status);
    }
  }
}

bool TsDemuxer::JumpDetected(Stream& stream, int64_t ticks) {
  bool jump = false;
  if (stream.last_ticks.has_value()) {
    const int64_t delta = ticks - *stream.last_ticks;
    jump = delta > kTsDiscontinuityTicks || delta < -kTsDiscontinuityTicks;
  }
  stream.last_ticks = ticks;
  return jump;
}

void TsDemuxer::HandleVideo(Stream& stream, std::span<const uint8_t> payload,
                            std::optional<int64_t> pts,
                            std::optional<int64_t> dts, bool random_access) {
  if (!pts.has_value()) {
    if (!stream.last_ticks.has_value()) {
      ++stats_.dropped_pes;
      return;
    }
    pts = *stream.last_ticks + stream.last_duration;
  }
  if (!dts.has_value()) {
    dts = pts;
  }
  const CaptionCodec codec_kind = stream.kind == ts::EsKind::kH264
                                      ? CaptionCodec::kH264
                                      : CaptionCodec::kHevc;
  const ts::VideoScan scan = ts::ScanVideo(codec_kind, payload);
  const bool key = scan.key_frame || random_access;
  std::shared_ptr<const penumbra::StreamInfo> change;
  if (scan.has_parameter_sets) {
    absl::StatusOr<penumbra::StreamInfo> built = ts::BuildVideoInfo(
        codec_kind, payload, stream.pmt.pid, stream.pmt.descriptors.language);
    if (built.ok()) {
      if (!stream.info.has_value()) {
        stream.info = std::move(*built);
      } else if (!SameConfig(*stream.info, *built)) {
        stream.info = std::move(*built);
        change = std::make_shared<const penumbra::StreamInfo>(*stream.info);
      }
    }
  }
  if (!stream.info.has_value()) {
    return;
  }
  if (stream.skip_until_key) {
    if (!key) {
      return;
    }
    stream.skip_until_key = false;
  }
  if (change == nullptr && stream.config_change_pending) {
    change = std::make_shared<const penumbra::StreamInfo>(*stream.info);
  }
  stream.config_change_pending = false;
  const bool jump = JumpDetected(stream, *dts);
  TsSampleParts sample;
  sample.track_id = stream.pmt.pid;
  sample.data = penumbra::SharedBytes::Copy(payload);
  sample.pts = Ticks(*pts);
  sample.dts = Ticks(*dts);
  sample.key_frame = key;
  sample.discontinuity = stream.discontinuity_pending || jump;
  sample.config_change = std::move(change);
  stream.discontinuity_pending = false;
  if (options_.extract_captions && scan.has_sei) {
    EmitCaptions(stream, stream.kind, sample);
  }
  PushHeld(stream, std::move(sample));
}

void TsDemuxer::EmitCaptions(Stream& stream, ts::EsKind kind,
                             const TsSampleParts& sample) {
  const absl::StatusOr<std::vector<uint8_t>> prefixed =
      codec::AnnexBToLengthPrefixed(sample.data.span(), 4);
  if (!prefixed.ok()) {
    return;
  }
  std::vector<std::vector<uint8_t>> payloads = ExtractCaptionData(
      kind == ts::EsKind::kH264 ? CaptionCodec::kH264 : CaptionCodec::kHevc, 4,
      *prefixed);
  for (std::vector<uint8_t>& payload : payloads) {
    if (stream.caption_stream_id == 0) {
      MetadataStreamInfo info;
      info.stream_id = next_metadata_stream_id_++;
      info.kind = MetadataKind::kCaptions;
      info.time_base = ts::kTimeBase;
      info.source_track_id = sample.track_id;
      stream.caption_stream_id = info.stream_id;
      EmitMetadataStream(info);
    }
    TimedMetadataItem item;
    item.stream_id = stream.caption_stream_id;
    item.time = sample.pts;
    item.payload = penumbra::SharedBytes::Adopt(std::move(payload));
    EmitItem(std::move(item));
  }
}

void TsDemuxer::PushHeld(Stream& stream, TsSampleParts sample) {
  if (stream.held.has_value()) {
    TsSampleParts previous = std::move(*stream.held);
    int64_t delta = sample.dts.value - previous.dts.value;
    if (sample.discontinuity || delta <= 0) {
      delta = stream.last_duration;
    } else {
      stream.last_duration = delta;
    }
    if (delta > 0) {
      previous.duration = Ticks(delta);
    }
    EmitSample(std::move(previous));
  }
  stream.held = std::move(sample);
}

void TsDemuxer::FlushHeld(Stream& stream) {
  if (!stream.held.has_value()) {
    return;
  }
  TsSampleParts sample = std::move(*stream.held);
  stream.held.reset();
  if (stream.last_duration > 0) {
    sample.duration = Ticks(stream.last_duration);
  }
  EmitSample(std::move(sample));
}

void TsDemuxer::HandleAudio(Stream& stream, std::span<const uint8_t> payload,
                            std::optional<int64_t> pts) {
  if (!pts.has_value()) {
    pts = stream.next_ticks;
  }
  if (!pts.has_value()) {
    ++stats_.dropped_pes;
    return;
  }
  const ts::AudioSplit split = ts::SplitAudio(stream.kind, payload);
  if (split.lost_sync) {
    ++stats_.audio_resyncs;
    stream.discontinuity_pending = true;
  }
  if (split.frames.empty()) {
    return;
  }
  const ts::AudioFrame& first = split.frames.front();
  std::shared_ptr<const penumbra::StreamInfo> change;
  absl::StatusOr<penumbra::StreamInfo> built =
      ts::BuildAudioInfo(stream.kind, payload.subspan(first.offset, first.size),
                         stream.pmt.pid, stream.pmt.descriptors.language);
  if (built.ok()) {
    if (!stream.info.has_value()) {
      stream.info = std::move(*built);
    } else if (!SameConfig(*stream.info, *built)) {
      stream.info = std::move(*built);
      change = std::make_shared<const penumbra::StreamInfo>(*stream.info);
    }
  }
  if (!stream.info.has_value()) {
    return;
  }
  if (change == nullptr && stream.config_change_pending) {
    change = std::make_shared<const penumbra::StreamInfo>(*stream.info);
  }
  stream.config_change_pending = false;
  const bool jump = JumpDetected(stream, *pts);
  bool first_frame = true;
  int64_t total_samples = 0;
  for (const ts::AudioFrame& frame : split.frames) {
    const int64_t start = *pts + SampleTicks(total_samples, frame.sample_rate);
    total_samples += frame.samples;
    const int64_t end = *pts + SampleTicks(total_samples, frame.sample_rate);
    TsSampleParts sample;
    sample.track_id = stream.pmt.pid;
    sample.data =
        penumbra::SharedBytes::Copy(payload.subspan(frame.offset, frame.size));
    sample.pts = Ticks(start);
    sample.dts = Ticks(start);
    sample.duration = Ticks(end - start);
    sample.key_frame = true;
    if (first_frame) {
      sample.discontinuity = stream.discontinuity_pending || jump;
      sample.config_change = change;
      stream.discontinuity_pending = false;
      first_frame = false;
    }
    EmitSample(std::move(sample));
  }
  stream.next_ticks =
      *pts + SampleTicks(total_samples, split.frames.back().sample_rate);
}

void TsDemuxer::HandleId3(Stream& stream, std::span<const uint8_t> payload,
                          std::optional<int64_t> pts) {
  if (!pts.has_value() || payload.empty()) {
    return;
  }
  if (stream.metadata_stream_id == 0) {
    MetadataStreamInfo info;
    info.stream_id = next_metadata_stream_id_++;
    info.kind = MetadataKind::kId3;
    info.time_base = ts::kTimeBase;
    stream.metadata_stream_id = info.stream_id;
    EmitMetadataStream(info);
  }
  TimedMetadataItem item;
  item.stream_id = stream.metadata_stream_id;
  item.time = Ticks(*pts);
  item.payload = penumbra::SharedBytes::Copy(payload);
  EmitItem(std::move(item));
  stream.discontinuity_pending = false;
}

penumbra::EncodedSample TsDemuxer::ToEncodedSample(TsSampleParts parts) {
  penumbra::EncodedSample sample;
  sample.track_id = parts.track_id;
  sample.data = std::move(parts.data);
  sample.pts = parts.pts;
  sample.dts = parts.dts;
  sample.duration = parts.duration;
  sample.key_frame = parts.key_frame;
  sample.discontinuity = parts.discontinuity;
  sample.config_change = std::move(parts.config_change);
  return sample;
}

void TsDemuxer::EmitSample(TsSampleParts sample) {
  if (announced_) {
    output_.OnSample(ToEncodedSample(std::move(sample)));
  } else {
    pending_.emplace_back(std::move(sample));
  }
}

void TsDemuxer::EmitMetadataStream(const MetadataStreamInfo& stream) {
  if (announced_) {
    output_.OnMetadataStream(stream);
  } else {
    pending_.emplace_back(stream);
  }
}

void TsDemuxer::EmitItem(TimedMetadataItem item) {
  if (announced_) {
    output_.OnTimedMetadata(std::move(item));
  } else {
    pending_.emplace_back(std::move(item));
  }
}

void TsDemuxer::FlushPending() {
  while (!pending_.empty()) {
    PendingEvent event = std::move(pending_.front());
    pending_.pop_front();
    if (auto* sample = std::get_if<TsSampleParts>(&event)) {
      output_.OnSample(ToEncodedSample(std::move(*sample)));
    } else if (auto* stream = std::get_if<MetadataStreamInfo>(&event)) {
      output_.OnMetadataStream(*stream);
    } else {
      output_.OnTimedMetadata(std::move(std::get<TimedMetadataItem>(event)));
    }
  }
}

void TsDemuxer::ClearPending() {
  pending_.clear();
  next_metadata_stream_id_ = 1;
  for (Stream& stream : streams_) {
    stream.metadata_stream_id = 0;
    stream.caption_stream_id = 0;
  }
}

absl::Status TsDemuxer::TryAnnounce(bool force) {
  if (announced_) {
    return absl::OkStatus();
  }
  if (streams_.empty()) {
    if (!force) {
      return absl::OkStatus();
    }
    if (!pmt_seen_) {
      return absl::DataLossError("the stream has no program map table");
    }
    return absl::UnimplementedError("the program has no playable stream");
  }
  const bool ready = std::ranges::all_of(streams_, [](const Stream& stream) {
    return !IsMedia(stream.kind) || stream.dropped || stream.info.has_value();
  });
  if (!ready && !force && probe_packets_ < kTsProbePackets) {
    return absl::OkStatus();
  }
  std::vector<penumbra::StreamInfo> infos;
  for (Stream& stream : streams_) {
    if (!IsMedia(stream.kind) || stream.dropped) {
      continue;
    }
    if (stream.info.has_value()) {
      infos.push_back(*stream.info);
      continue;
    }
    stream.dropped = true;
    ++stats_.skipped_streams;
    const uint32_t track = stream.pmt.pid;
    std::erase_if(pending_, [track](const PendingEvent& event) {
      const auto* sample = std::get_if<TsSampleParts>(&event);
      return sample != nullptr && sample->track_id == track;
    });
  }
  if (infos.empty()) {
    return absl::UnimplementedError("the program has no playable stream");
  }
  announced_ = true;
  output_.OnStreams(std::move(infos));
  if (!opened_) {
    opened_ = true;
    if (TailApplicable()) {
      ClearPending();
      BeginTail();
      return absl::OkStatus();
    }
    if (options_.probe_duration && source_size_.has_value() && seekable_) {
      map_deferred_ = true;
    } else {
      EmitSeekMap(false);
    }
  }
  FlushPending();
  return absl::OkStatus();
}

bool TsDemuxer::TailApplicable() const {
  return options_.probe_duration && source_size_.has_value() && seekable_ &&
         first_ticks_.has_value() && !AtEnd() && *source_size_ > kTsTailBytes &&
         AlignDown(*source_size_ - kTsTailBytes) >= queue_.EndOffset();
}

void TsDemuxer::BeginTail() {
  mode_ = Mode::kTail;
  unwrapper_.Seed(first_ticks_.value_or(0));
  ResetStreamsForRestart(false);
  const uint64_t size = source_size_.value_or(0);
  seek_request_ = AlignDown(size > kTsTailBytes ? size - kTsTailBytes : 0);
}

void TsDemuxer::FinishTail() {
  EmitSeekMap(true);
  mode_ = Mode::kStream;
  unwrapper_.Seed(first_ticks_.value_or(0));
  ResetStreamsForRestart(false);
  seek_request_ = sync_base_;
}

void TsDemuxer::EmitSeekMap(bool seekable) {
  map_emitted_ = true;
  map_deferred_ = false;
  seek_map_seekable_ = false;
  if (seekable && first_ticks_.has_value() && max_ticks_.has_value() &&
      *max_ticks_ > *first_ticks_ && source_size_.has_value()) {
    const std::optional<penumbra::MediaTime> start =
        penumbra::ToMediaTime(Ticks(*first_ticks_), penumbra::Rounding::kDown);
    const std::optional<penumbra::MediaTime> end =
        penumbra::ToMediaTime(Ticks(*max_ticks_), penumbra::Rounding::kDown);
    if (start.has_value() && end.has_value()) {
      seek_map_seekable_ = true;
      output_.OnSeekMap(
          std::make_shared<TsSeekMap>(*start, *end, sync_base_, *source_size_));
      return;
    }
  }
  output_.OnSeekMap(std::make_shared<TsSeekMap>());
}

void TsDemuxer::ResetStreamsForRestart(bool discontinuity) {
  for (Stream& stream : streams_) {
    stream.pes_active = false;
    stream.pes.clear();
    stream.held.reset();
    stream.last_ticks.reset();
    stream.next_ticks.reset();
    stream.skip_until_key = false;
    stream.discontinuity_pending = discontinuity;
  }
}

TsDemuxer::Stream* TsDemuxer::ReferenceStream() {
  for (Stream& stream : streams_) {
    if (IsVideo(stream.kind) && stream.info.has_value() && !stream.dropped) {
      return &stream;
    }
  }
  for (Stream& stream : streams_) {
    if (IsMedia(stream.kind) && stream.info.has_value() && !stream.dropped) {
      return &stream;
    }
  }
  return nullptr;
}

void TsDemuxer::ProbePes(Stream& stream, std::span<const uint8_t> pes,
                         uint64_t offset, bool random_access) {
  if (!IsMedia(stream.kind) || stream.dropped) {
    return;
  }
  const std::optional<ts::PesHeader> header = ts::ParsePesHeader(pes);
  if (!header.has_value() || !header->pts.has_value()) {
    return;
  }
  if (mode_ == Mode::kBisect && &stream != ReferenceStream()) {
    return;
  }
  if (header->dts.has_value()) {
    static_cast<void>(unwrapper_.Unwrap(*header->dts));
  }
  const int64_t pts = unwrapper_.Unwrap(*header->pts);
  if (mode_ == Mode::kTail) {
    if (!max_ticks_.has_value() || pts > *max_ticks_) {
      max_ticks_ = pts;
    }
    return;
  }
  if (offset >= bisect_.high || bisect_.found.has_value()) {
    return;
  }
  bool key = true;
  if (IsVideo(stream.kind) && header->payload_offset <= pes.size()) {
    const ts::VideoScan scan =
        ts::ScanVideo(stream.kind == ts::EsKind::kH264 ? CaptionCodec::kH264
                                                       : CaptionCodec::kHevc,
                      pes.subspan(header->payload_offset));
    key = scan.key_frame || random_access;
  }
  if (key) {
    bisect_.found = pts;
  }
}

void TsDemuxer::EndProbe() {
  if (!bisect_.found.has_value()) {
    Stream* reference = ReferenceStream();
    if (reference != nullptr && reference->pes_active) {
      CompletePes(*reference);
    }
  }
  if (bisect_.found.has_value() && *bisect_.found <= bisect_.target) {
    bisect_.low = bisect_.probe;
  } else {
    bisect_.high = bisect_.probe;
  }
  ++bisect_.probes;
  bisect_.found.reset();
  const uint64_t span = bisect_.high - bisect_.low;
  const uint64_t next = AlignDown(bisect_.low + (span / 2));
  if (span <= kTsSeekGranularity || bisect_.probes >= kTsMaxSeekProbes ||
      next <= bisect_.low) {
    PrepareStreaming(bisect_.target);
    seek_request_ = bisect_.low;
    return;
  }
  bisect_.probe = next;
  ResetStreamsForRestart(false);
  unwrapper_.Seed(bisect_.target);
  seek_request_ = next;
}

void TsDemuxer::PrepareStreaming(int64_t target) {
  mode_ = Mode::kStream;
  ResetStreamsForRestart(true);
  for (Stream& stream : streams_) {
    stream.skip_until_key = IsVideo(stream.kind) && stream.info.has_value();
  }
  unwrapper_.Seed(target);
}

absl::StatusOr<uint64_t> TsDemuxer::Seek(const SeekPoint& point) {
  if (!announced_ || !seek_map_seekable_ || !source_size_.has_value() ||
      !first_ticks_.has_value()) {
    return absl::FailedPreconditionError(
        "the transport stream has no usable seek map");
  }
  const std::optional<int64_t> target = penumbra::Rescale(
      point.time.InMicroseconds(), penumbra::kMicrosecondTimeBase,
      ts::kTimeBase, penumbra::Rounding::kDown);
  if (!point.time.is_finite() || !target.has_value()) {
    return absl::InvalidArgumentError("the seek time is not finite");
  }
  failure_ = absl::OkStatus();
  seek_request_.reset();
  mode_ = Mode::kStream;
  uint64_t offset = sync_base_;
  if (*target > *first_ticks_) {
    const uint64_t size = *source_size_;
    bisect_ = Bisect{.low = sync_base_,
                     .high = size,
                     .probe = 0,
                     .target = *target,
                     .probes = 0,
                     .found = std::nullopt};
    const uint64_t last = AlignDown(size > 0 ? size - 1 : 0);
    bisect_.probe = std::clamp(AlignDown(point.byte_offset), sync_base_, last);
    mode_ = Mode::kBisect;
    ResetStreamsForRestart(false);
    unwrapper_.Seed(*target);
    offset = bisect_.probe;
  } else {
    PrepareStreaming(*first_ticks_);
  }
  MoveTo(offset);
  return offset;
}

void TsDemuxer::Reset(uint64_t offset) {
  failure_ = absl::OkStatus();
  seek_request_.reset();
  mode_ = Mode::kStream;
  pending_.clear();
  ResetStreamsForRestart(true);
  MoveTo(offset);
}

}
