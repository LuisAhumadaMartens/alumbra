#ifndef UMBRA_DEMUX_TS_DEMUXER_H_
#define UMBRA_DEMUX_TS_DEMUXER_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/demux/byte_queue.h"
#include "umbra/demux/demuxer.h"
#include "umbra/demux/ts_framing.h"
#include "umbra/demux/ts_packet.h"

namespace umbra::demux {

inline constexpr size_t kTsStepBudgetPackets = 512;
inline constexpr size_t kTsPreferredReadBytes = size_t{1} << 16;
inline constexpr size_t kTsMaxPesBytes = size_t{1} << 24;
inline constexpr size_t kTsProbePackets = 40000;
inline constexpr uint64_t kTsTailBytes = uint64_t{1} << 20;
inline constexpr uint64_t kTsSeekGranularity = uint64_t{1} << 18;
inline constexpr size_t kTsMaxSeekProbes = 64;
inline constexpr int64_t kTsDiscontinuityTicks = int64_t{10} * 90000;

struct TsDemuxerOptions {
  uint32_t program_number = 0;
  bool extract_captions = true;
  bool probe_duration = true;
};

struct TsDemuxerStats {
  size_t sync_losses = 0;
  size_t corrupt_packets = 0;
  size_t transport_errors = 0;
  size_t continuity_errors = 0;
  size_t duplicate_packets = 0;
  size_t crc_errors = 0;
  size_t scrambled_packets = 0;
  size_t pmt_changes = 0;
  size_t skipped_streams = 0;
  size_t dropped_pes = 0;
  size_t audio_resyncs = 0;
  size_t pcr_count = 0;
};

class TsDemuxer final : public Demuxer {
 public:
  explicit TsDemuxer(DemuxerOutput& output, TsDemuxerOptions options = {});

  void SetSourceInfo(std::optional<uint64_t> size, bool seekable) override;
  [[nodiscard]] uint64_t NextOffset() const override;
  [[nodiscard]] absl::Status Append(uint64_t offset,
                                    penumbra::SharedBytes bytes) override;
  void EndOfInput() override;
  [[nodiscard]] absl::StatusOr<StepResult> Step() override;
  [[nodiscard]] absl::StatusOr<uint64_t> Seek(const SeekPoint& point) override;
  void Reset(uint64_t offset) override;

  [[nodiscard]] const TsDemuxerStats& Stats() const { return stats_; }

 private:
  enum class Mode : uint8_t { kStream, kTail, kBisect };

  struct Stream {
    ts::PmtStream pmt;
    ts::EsKind kind = ts::EsKind::kH264;
    std::optional<penumbra::StreamInfo> info;
    bool dropped = false;
    std::vector<uint8_t> pes;
    bool pes_active = false;
    size_t pes_expected = 0;
    uint64_t pes_offset = 0;
    bool pes_random_access = false;
    bool discontinuity_pending = false;
    bool config_change_pending = false;
    bool skip_until_key = false;
    std::optional<penumbra::EncodedSample> held;
    int64_t last_duration = 0;
    std::optional<int64_t> last_ticks;
    std::optional<int64_t> next_ticks;
    uint32_t metadata_stream_id = 0;
    uint32_t caption_stream_id = 0;
  };

  using PendingEvent = std::variant<penumbra::EncodedSample, MetadataStreamInfo,
                                    TimedMetadataItem>;

  struct Bisect {
    uint64_t low = 0;
    uint64_t high = 0;
    uint64_t probe = 0;
    int64_t target = 0;
    size_t probes = 0;
    std::optional<int64_t> found;
  };

  using Advance = absl::StatusOr<std::optional<StepResult>>;

  Advance AdvancePacket();
  Advance AcquireSync();
  Advance FinishInput();
  Advance TakeRequest();
  [[nodiscard]] Advance NeedMore(size_t wanted) const;
  [[nodiscard]] bool AtEnd() const;
  void MoveTo(uint64_t offset);
  void LoseSync();
  void DropAllPartial();
  void MarkAllDiscontinuous();

  absl::Status HandlePacket(std::span<const uint8_t> packet, uint64_t offset);
  static void DropPartial(Stream& stream);
  bool AdmitPacket(const ts::PacketHeader& header, Stream* stream, bool is_pat,
                   bool is_pmt);
  void ApplyDiscontinuityIndicator(const ts::PacketHeader& header,
                                   Stream* stream);
  void HandlePsi(const ts::PacketHeader& header,
                 ts::SectionAssembler& assembler, bool is_pat);
  void HandleEsPacket(Stream& stream, const ts::PacketHeader& header,
                      uint64_t offset);
  enum class Continuity : uint8_t { kOk, kDuplicate, kLost };
  Continuity CheckContinuity(const ts::PacketHeader& header);

  void OnPat(const ts::Pat& pat);
  void OnPmt(const ts::Pmt& pmt);
  [[nodiscard]] static std::optional<ts::EsKind> Classify(
      const ts::PmtStream& stream);
  [[nodiscard]] int StreamIndex(uint16_t pid) const;
  void RebuildIndex();

  void CompletePes(Stream& stream);
  void CompleteOrDrop(Stream& stream);
  void HandlePes(Stream& stream, std::span<const uint8_t> pes,
                 bool random_access);
  void ProbePes(Stream& stream, std::span<const uint8_t> pes, uint64_t offset,
                bool random_access);
  void HandleVideo(Stream& stream, std::span<const uint8_t> payload,
                   std::optional<int64_t> pts, std::optional<int64_t> dts,
                   bool random_access);
  void HandleAudio(Stream& stream, std::span<const uint8_t> payload,
                   std::optional<int64_t> pts);
  void HandleId3(Stream& stream, std::span<const uint8_t> payload,
                 std::optional<int64_t> pts);
  void EmitCaptions(Stream& stream, ts::EsKind kind,
                    const penumbra::EncodedSample& sample);
  void PushHeld(Stream& stream, penumbra::EncodedSample sample);
  void FlushHeld(Stream& stream);
  [[nodiscard]] static bool JumpDetected(Stream& stream, int64_t ticks);
  void NoteTimes(int64_t first, int64_t pts);

  void EmitSample(penumbra::EncodedSample sample);
  void EmitMetadataStream(const MetadataStreamInfo& stream);
  void EmitItem(TimedMetadataItem item);
  void FlushPending();
  void ClearPending();

  absl::Status TryAnnounce(bool force);
  [[nodiscard]] bool TailApplicable() const;
  void BeginTail();
  void FinishTail();
  void EmitSeekMap(bool seekable);
  [[nodiscard]] uint64_t AlignDown(uint64_t offset) const;
  [[nodiscard]] Stream* ReferenceStream();
  void EndProbe();
  void FinishSeek(uint64_t offset);
  void ResetStreamsForRestart(bool discontinuity);
  void PrepareStreaming(int64_t target);
  void RecordFailure(const absl::Status& status);

  DemuxerOutput& output_;
  TsDemuxerOptions options_;
  TsDemuxerStats stats_;
  ByteQueue queue_;
  std::optional<uint64_t> source_size_;
  bool seekable_ = true;
  bool ended_ = false;
  bool finished_ = false;
  absl::Status failure_;

  bool locked_ = false;
  bool ever_locked_ = false;
  size_t packet_size_ = ts::kPacketSize;
  uint64_t sync_base_ = 0;
  std::array<int16_t, 8192> last_continuity_{};

  ts::SectionAssembler pat_assembler_;
  ts::SectionAssembler pmt_assembler_;
  std::vector<ts::PatEntry> programs_;
  std::optional<uint8_t> pat_version_;
  std::optional<uint16_t> selected_program_;
  std::optional<uint16_t> pmt_pid_;
  uint16_t pcr_pid_ = ts::kNullPid;
  bool pmt_seen_ = false;
  std::vector<Stream> streams_;
  std::array<int16_t, 8192> stream_index_{};
  ts::TimestampUnwrapper unwrapper_;

  bool announced_ = false;
  bool opened_ = false;
  size_t probe_packets_ = 0;
  std::deque<PendingEvent> pending_;
  uint32_t next_metadata_stream_id_ = 1;

  Mode mode_ = Mode::kStream;
  Bisect bisect_;
  std::optional<uint64_t> seek_request_;
  std::optional<int64_t> first_ticks_;
  std::optional<int64_t> max_ticks_;
  bool map_emitted_ = false;
  bool map_deferred_ = false;
  bool seek_map_seekable_ = false;
};

}

#endif
