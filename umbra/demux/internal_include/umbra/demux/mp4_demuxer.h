#ifndef UMBRA_DEMUX_MP4_DEMUXER_H_
#define UMBRA_DEMUX_MP4_DEMUXER_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/shared_bytes.h"
#include "umbra/demux/byte_queue.h"
#include "umbra/demux/demuxer.h"
#include "umbra/demux/mp4_box.h"
#include "umbra/demux/mp4_fragment.h"
#include "umbra/demux/mp4_types.h"

namespace umbra::demux {

inline constexpr uint64_t kMaxSkipBytes = 256ULL * 1024;
inline constexpr size_t kStepBudgetUnits = 128;
inline constexpr size_t kMaxPendingEmsg = 256;

struct Mp4DemuxerOptions {
  bool edit_lists_enabled = true;
  bool extract_captions = true;
};

struct Mp4DemuxerStats {
  size_t ignored_edit_entries = 0;
  size_t skipped_tracks = 0;
  size_t unsupported_emsg_boxes = 0;
};

class Mp4Demuxer final : public Demuxer {
 public:
  explicit Mp4Demuxer(DemuxerOutput& output, Mp4DemuxerOptions options = {});

  void SetSourceInfo(std::optional<uint64_t> size, bool seekable) override;
  [[nodiscard]] uint64_t NextOffset() const override;
  [[nodiscard]] absl::Status Append(uint64_t offset,
                                    penumbra::SharedBytes bytes) override;
  void EndOfInput() override;
  [[nodiscard]] absl::StatusOr<StepResult> Step() override;
  [[nodiscard]] absl::StatusOr<uint64_t> Seek(const SeekPoint& point) override;
  void Reset(uint64_t offset) override;

  [[nodiscard]] const Mp4DemuxerStats& Stats() const { return stats_; }

 private:
  struct Track {
    size_t def_index = 0;
    TrackTiming timing;
    std::shared_ptr<const SampleList> samples;
    size_t cursor = 0;
    uint16_t current_description = 0;
    bool discontinuity_pending = false;
    bool config_change_pending = false;
    uint32_t caption_stream_id = 0;
  };

  struct PendingEmsg {
    uint32_t stream_id = 0;
    Emsg emsg;
  };

  using Advance = absl::StatusOr<std::optional<StepResult>>;

  Advance AdvanceBoxes();
  Advance AdvanceSamples();
  Advance MoveTo(uint64_t target);
  Advance NeedMore(size_t wanted);
  Advance HandleBox(const BoxHeader& header);
  Advance HandleMdat(uint64_t start, uint64_t total);
  Advance FinishStream();
  Advance ReadWholeBox(const BoxHeader& header, uint64_t total);
  absl::Status DispatchBox(uint32_t type, const penumbra::SharedBytes& box,
                           size_t header_size, uint64_t start);

  absl::Status OnMoov(std::span<const uint8_t> payload);
  absl::Status OnMoof(std::span<const uint8_t> payload, uint64_t start,
                      uint64_t total);
  absl::Status OnSidx(std::span<const uint8_t> payload, uint64_t box_end);
  absl::Status OnEmsg(std::span<const uint8_t> payload);
  absl::Status OnPrft(std::span<const uint8_t> payload);
  absl::Status BuildTracks(Movie movie);
  void AnnounceMovie();
  void ApplyFragment(Fragment fragment);
  void FlushPendingEmsg(const Fragment& fragment);
  void EmitPendingEmsgUnknown();
  void EmitEmsg(uint32_t stream_id, const Emsg& emsg, penumbra::Timestamp time);
  uint32_t EmsgStreamFor(const Emsg& emsg);
  absl::Status EmitSample(Track& track, const SampleRef& ref,
                          penumbra::SharedBytes data);
  void EmitCaptions(Track& track, const SampleEntryInfo& entry,
                    const penumbra::EncodedSample& sample);
  void FinishSamplePhase();
  void ResetState(uint64_t offset);
  [[nodiscard]] size_t NextTrack() const;
  [[nodiscard]] const SampleEntryInfo* EntryFor(const Track& track,
                                                uint16_t description) const;

  DemuxerOutput& output_;
  Mp4DemuxerOptions options_;
  Mp4DemuxerStats stats_;
  ByteQueue queue_;
  std::optional<uint64_t> source_size_;
  bool seekable_ = true;
  bool ended_ = false;
  bool finished_ = false;
  bool saw_box_ = false;
  bool moov_seen_ = false;
  bool progressive_ = false;
  bool sample_phase_ = false;
  bool fragment_pending_ = false;
  absl::Status failure_;
  uint64_t next_box_ = 0;
  uint64_t resume_box_ = 0;
  Movie movie_;
  std::vector<Track> tracks_;
  std::vector<std::optional<int64_t>> expected_dts_;
  std::vector<PendingEmsg> pending_emsg_;
  std::map<std::pair<std::string, std::string>, uint32_t> emsg_streams_;
  uint32_t next_metadata_stream_id_ = 1;
  std::map<int64_t, SeekPoint> index_points_;
  std::optional<uint32_t> index_reference_id_;
  int64_t index_end_micros_ = 0;
};

}

#endif
