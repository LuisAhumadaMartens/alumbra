#ifndef UMBRA_DEMUX_MKV_DEMUXER_H_
#define UMBRA_DEMUX_MKV_DEMUXER_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/shared_bytes.h"
#include "umbra/demux/byte_queue.h"
#include "umbra/demux/demuxer.h"
#include "umbra/demux/ebml.h"
#include "umbra/demux/mkv_block.h"
#include "umbra/demux/mkv_elements.h"
#include "umbra/demux/mkv_tracks.h"

namespace umbra::demux {

inline constexpr uint64_t kMatroskaMaxSkipBytes = 256ULL * 1024;
inline constexpr size_t kMatroskaStepBudgetUnits = 128;
inline constexpr uint64_t kMatroskaMaxResyncBytes = 16ULL * 1024 * 1024;
inline constexpr size_t kMatroskaResyncWindowBytes = size_t{64} * 1024;

struct MatroskaDemuxerStats {
  size_t skipped_tracks = 0;
  size_t dropped_blocks = 0;
  size_t resyncs = 0;
};

struct MkvBlockInput {
  const penumbra::SharedBytes* element = nullptr;
  size_t block_offset = 0;
  size_t block_size = 0;
  bool from_group = false;
  bool has_reference = false;
  std::optional<uint64_t> duration;
  std::optional<int64_t> discard_padding_ns;
};

struct MkvFramePlan {
  int64_t pts = 0;
  std::optional<int64_t> duration;
  std::optional<int64_t> padding_ticks;
  bool padding_negative = false;
  bool first = false;
  bool last = false;
  bool key_frame = false;
};

class MatroskaDemuxer final : public Demuxer {
 public:
  explicit MatroskaDemuxer(DemuxerOutput& output);

  void SetSourceInfo(std::optional<uint64_t> size, bool seekable) override;
  [[nodiscard]] uint64_t NextOffset() const override;
  [[nodiscard]] absl::Status Append(uint64_t offset,
                                    penumbra::SharedBytes bytes) override;
  void EndOfInput() override;
  [[nodiscard]] absl::StatusOr<StepResult> Step() override;
  [[nodiscard]] absl::StatusOr<uint64_t> Seek(const SeekPoint& point) override;
  void Reset(uint64_t offset) override;

  [[nodiscard]] const MatroskaDemuxerStats& Stats() const { return stats_; }

 private:
  struct Track {
    MkvTrackConfig config;
    bool video = false;
    bool discontinuity_pending = false;
    bool config_change_pending = false;
  };

  using Advance = absl::StatusOr<std::optional<StepResult>>;
  using Handler = absl::Status (MatroskaDemuxer::*)(
      const penumbra::SharedBytes& element, size_t header_size);

  Advance AdvanceElements();
  Advance AdvanceResync();
  Advance Malformed(const absl::Status& why);
  Advance MoveTo(uint64_t target);
  Advance NeedMore(size_t wanted);
  Advance FinishStream();
  Advance HandleElement(const EbmlHeader& header);
  Advance HandleSegmentChild(const EbmlHeader& header, uint64_t start);
  Advance HandleClusterChild(const EbmlHeader& header, uint64_t start);
  Advance EnterSegment(const EbmlHeader& header, uint64_t start);
  Advance EnterCluster(const EbmlHeader& header, uint64_t start);
  Advance SkipElement(const EbmlHeader& header, uint64_t start);
  Advance Buffered(const EbmlHeader& header, uint64_t start, uint64_t limit,
                   Handler handler);
  [[nodiscard]] absl::Status CheckWithinParents(uint64_t end) const;
  void AbandonCues();

  absl::Status OnEbmlHeader(const penumbra::SharedBytes& element,
                            size_t header_size);
  absl::Status OnSeekHead(const penumbra::SharedBytes& element,
                          size_t header_size);
  absl::Status OnInfo(const penumbra::SharedBytes& element, size_t header_size);
  absl::Status OnTracks(const penumbra::SharedBytes& element,
                        size_t header_size);
  absl::Status OnCues(const penumbra::SharedBytes& element, size_t header_size);
  absl::Status OnClusterTimestamp(const penumbra::SharedBytes& element,
                                  size_t header_size);
  absl::Status OnSimpleBlock(const penumbra::SharedBytes& element,
                             size_t header_size);
  absl::Status OnBlockGroup(const penumbra::SharedBytes& element,
                            size_t header_size);

  absl::Status Announce();
  void EmitSeekMap(std::vector<SeekPoint> points);
  void EmitCueMap();
  void AddScanPoint(uint64_t ticks, uint64_t offset);
  absl::Status EmitBlock(const MkvBlockInput& input);
  absl::Status EmitFrames(Track& track, const MkvBlockInput& input,
                          const MkvBlock& block, int64_t first_pts);
  void EmitSample(Track& track, const penumbra::SharedBytes& frame,
                  const MkvFramePlan& plan);
  [[nodiscard]] Track* FindTrack(uint64_t number);
  void ResetSegmentState();
  void ResetState(uint64_t offset);

  DemuxerOutput& output_;
  MatroskaDemuxerStats stats_;
  ByteQueue queue_;
  std::optional<uint64_t> source_size_;
  bool seekable_ = true;
  bool ended_ = false;
  bool finished_ = false;
  bool resyncing_ = false;
  uint64_t resync_scanned_ = 0;
  absl::Status failure_;
  uint64_t next_element_ = 0;
  bool header_seen_ = false;
  bool in_segment_ = false;
  uint64_t segment_data_start_ = 0;
  uint64_t segment_end_ = kEbmlUnknownSize;
  bool in_cluster_ = false;
  uint64_t cluster_start_ = 0;
  uint64_t cluster_end_ = kEbmlUnknownSize;
  std::optional<uint64_t> cluster_ticks_;
  std::optional<uint64_t> last_cluster_ticks_;
  MkvInfo info_;
  bool info_seen_ = false;
  std::vector<MkvTrack> raw_tracks_;
  bool tracks_pending_ = false;
  bool announced_ = false;
  std::vector<Track> tracks_;
  penumbra::Rational scale_base_;
  penumbra::TimeDelta duration_ = penumbra::TimeDelta::Unknown();
  std::optional<uint64_t> cues_position_;
  std::vector<MkvCuePoint> cue_points_;
  bool cues_seen_ = false;
  bool cues_attempted_ = false;
  bool fetching_cues_ = false;
  uint64_t resume_offset_ = 0;
  std::map<int64_t, SeekPoint> scan_points_;
  bool scan_dirty_ = false;
};

}

#endif
