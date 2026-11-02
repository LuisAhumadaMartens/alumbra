#ifndef UMBRA_DEMUX_DEMUXER_H_
#define UMBRA_DEMUX_DEMUXER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"

namespace umbra::demux {

enum class StepKind : uint8_t {
  kNeedMoreData,
  kSeek,
  kYield,
  kEndOfStream,
};

struct StepResult {
  StepKind kind = StepKind::kNeedMoreData;
  uint64_t byte_offset = 0;
  size_t wanted_bytes = 0;

  static StepResult NeedMoreData(uint64_t next_offset, size_t wanted_bytes) {
    return StepResult{.kind = StepKind::kNeedMoreData,
                      .byte_offset = next_offset,
                      .wanted_bytes = wanted_bytes};
  }
  static StepResult Seek(uint64_t byte_offset) {
    return StepResult{
        .kind = StepKind::kSeek, .byte_offset = byte_offset, .wanted_bytes = 0};
  }
  static StepResult Yield() {
    return StepResult{
        .kind = StepKind::kYield, .byte_offset = 0, .wanted_bytes = 0};
  }
  static StepResult EndOfStream() {
    return StepResult{
        .kind = StepKind::kEndOfStream, .byte_offset = 0, .wanted_bytes = 0};
  }

  friend bool operator==(const StepResult&, const StepResult&) = default;
};

enum class MetadataKind : uint8_t {
  kEmsg,
  kId3,
  kCaptions,
};

struct MetadataStreamInfo {
  uint32_t stream_id = 0;
  MetadataKind kind = MetadataKind::kEmsg;
  penumbra::Rational time_base;
  std::string scheme;
  std::string value;
  uint32_t source_track_id = 0;
};

struct TimedMetadataItem {
  uint32_t stream_id = 0;
  penumbra::Timestamp time;
  penumbra::Timestamp duration;
  penumbra::SharedBytes payload;
  uint32_t event_id = 0;
};

struct ProducerReferenceTime {
  uint32_t track_id = 0;
  uint64_t ntp_timestamp = 0;
  penumbra::Timestamp media_time;
  uint32_t flags = 0;
};

struct SeekPoint {
  penumbra::MediaTime time;
  uint64_t byte_offset = 0;

  friend bool operator==(const SeekPoint&, const SeekPoint&) = default;
};

class SeekMap {
 public:
  SeekMap() = default;
  SeekMap(const SeekMap&) = delete;
  SeekMap& operator=(const SeekMap&) = delete;
  virtual ~SeekMap() = default;

  [[nodiscard]] virtual bool IsSeekable() const = 0;
  [[nodiscard]] virtual penumbra::TimeDelta Duration() const = 0;
  [[nodiscard]] virtual std::optional<SeekPoint> SeekPointFor(
      penumbra::MediaTime target) const = 0;
};

class DemuxerOutput {
 public:
  DemuxerOutput() = default;
  DemuxerOutput(const DemuxerOutput&) = delete;
  DemuxerOutput& operator=(const DemuxerOutput&) = delete;
  virtual ~DemuxerOutput() = default;

  virtual void OnStreams(std::vector<penumbra::StreamInfo> streams) = 0;
  virtual void OnMetadataStream(const MetadataStreamInfo& stream) = 0;
  virtual void OnSeekMap(std::shared_ptr<const SeekMap> seek_map) = 0;
  virtual void OnSample(penumbra::EncodedSample sample) = 0;
  virtual void OnTimedMetadata(TimedMetadataItem item) = 0;
  virtual void OnProducerReferenceTime(
      const ProducerReferenceTime& reference) = 0;
};

class Demuxer {
 public:
  Demuxer() = default;
  Demuxer(const Demuxer&) = delete;
  Demuxer& operator=(const Demuxer&) = delete;
  virtual ~Demuxer() = default;

  virtual void SetSourceInfo(std::optional<uint64_t> size, bool seekable) = 0;

  [[nodiscard]] virtual uint64_t NextOffset() const = 0;
  [[nodiscard]] virtual absl::Status Append(uint64_t offset,
                                            penumbra::SharedBytes bytes) = 0;
  virtual void EndOfInput() = 0;
  [[nodiscard]] virtual absl::StatusOr<StepResult> Step() = 0;

  [[nodiscard]] virtual absl::StatusOr<uint64_t> Seek(
      const SeekPoint& point) = 0;
  virtual void Reset(uint64_t offset) = 0;
};

}

#endif
