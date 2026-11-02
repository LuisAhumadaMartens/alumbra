#ifndef UMBRA_DEMUX_PACKED_AUDIO_DEMUXER_H_
#define UMBRA_DEMUX_PACKED_AUDIO_DEMUXER_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "umbra/demux/byte_queue.h"
#include "umbra/demux/demuxer.h"

namespace umbra::demux {

class PackedAudioDemuxer : public Demuxer {
 public:
  void SetSourceInfo(std::optional<uint64_t> size, bool seekable) override;
  [[nodiscard]] uint64_t NextOffset() const override;
  [[nodiscard]] absl::Status Append(uint64_t offset,
                                    penumbra::SharedBytes bytes) override;
  void EndOfInput() override;
  [[nodiscard]] absl::StatusOr<StepResult> Step() override;
  [[nodiscard]] absl::StatusOr<uint64_t> Seek(const SeekPoint& point) override;
  void Reset(uint64_t offset) override;

 protected:
  PackedAudioDemuxer(DemuxerOutput& output, bool adts);

 private:
  struct FrameHeader {
    size_t size = 0;
    uint32_t sample_rate = 0;
    uint32_t samples = 0;
    uint8_t channels = 0;
    uint8_t object_type = 0;
    uint8_t frequency_index = 0;
    uint8_t channel_configuration = 0;
  };

  [[nodiscard]] absl::StatusOr<FrameHeader> ParseHeader(
      const std::vector<uint8_t>& bytes) const;
  [[nodiscard]] absl::StatusOr<std::optional<StepResult>> StepId3v2(
      const std::vector<uint8_t>& prefix);
  [[nodiscard]] absl::StatusOr<std::optional<StepResult>> StepId3v1(
      const std::vector<uint8_t>& prefix);
  [[nodiscard]] absl::StatusOr<StepResult> StepAudioFrame();
  [[nodiscard]] absl::Status EmitFrame(const FrameHeader& header,
                                       uint64_t frame_offset,
                                       penumbra::SharedBytes frame);
  [[nodiscard]] absl::Status ProcessId3(size_t tag_size);
  [[nodiscard]] absl::Status Announce(const FrameHeader& header);
  void UpdateSeekMap();
  [[nodiscard]] bool ParseXing(const std::vector<uint8_t>& frame,
                               const FrameHeader& header);

  DemuxerOutput& output_;
  bool adts_ = false;
  ByteQueue queue_;
  std::optional<uint64_t> source_size_;
  bool seekable_ = true;
  bool ended_ = false;
  bool initialized_ = false;
  bool discontinuity_pending_ = false;
  bool metadata_announced_ = false;
  bool xing_seen_ = false;
  uint64_t metadata_stream_id_ = 1;
  uint64_t frames_ = 0;
  uint64_t total_samples_ = 0;
  uint64_t duration_samples_ = 0;
  uint32_t sample_rate_ = 0;
  uint32_t channels_ = 0;
  uint32_t samples_per_frame_ = 0;
  uint32_t encoder_delay_ = 0;
  uint32_t encoder_padding_ = 0;
  std::vector<SeekPoint> points_;
  absl::Status failure_;
};

class AdtsDemuxer final : public PackedAudioDemuxer {
 public:
  explicit AdtsDemuxer(DemuxerOutput& output)
      : PackedAudioDemuxer(output, true) {}
};

class Mp3Demuxer final : public PackedAudioDemuxer {
 public:
  explicit Mp3Demuxer(DemuxerOutput& output)
      : PackedAudioDemuxer(output, false) {}
};

}

#endif
