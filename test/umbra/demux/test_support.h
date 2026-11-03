#ifndef TEST_UMBRA_DEMUX_TEST_SUPPORT_H_
#define TEST_UMBRA_DEMUX_TEST_SUPPORT_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "absl/status/status.h"
#include "mp4_builder.h"
#include "penumbra/interface/encoded_sample.h"
#include "umbra/demux/demuxer.h"

namespace umbra::demux::test {

class RecordingOutput final : public DemuxerOutput {
 public:
  void OnStreams(std::vector<penumbra::StreamInfo> declared) override;
  void OnMetadataStream(const MetadataStreamInfo& stream) override;
  void OnSeekMap(std::shared_ptr<const SeekMap> map) override;
  void OnSample(penumbra::EncodedSample sample) override;
  void OnTimedMetadata(TimedMetadataItem item) override;
  void OnProducerReferenceTime(const ProducerReferenceTime& reference) override;

  [[nodiscard]] std::vector<penumbra::EncodedSample> SamplesOf(
      uint32_t track_id) const;

  std::vector<penumbra::StreamInfo> streams;
  size_t stream_announcements = 0;
  std::vector<MetadataStreamInfo> metadata_streams;
  std::vector<TimedMetadataItem> items;
  std::vector<ProducerReferenceTime> references;
  std::vector<penumbra::EncodedSample> samples;
  std::shared_ptr<const SeekMap> seek_map;
  size_t seek_map_count = 0;
};

inline int64_t Micros(int64_t count, int64_t unit, int64_t timescale) {
  return (count * unit * 1000000) / timescale;
}

struct DriveResult {
  absl::Status status;
  size_t seek_requests = 0;
  uint64_t bytes_fetched = 0;
  size_t steps = 0;
};

struct StopAfter {
  const RecordingOutput* output = nullptr;
  size_t samples = 0;
};

DriveResult Drive(Demuxer& demuxer, const BuiltFile& file, size_t chunk_bytes,
                  StopAfter stop = {});
DriveResult DriveBytes(Demuxer& demuxer, const Bytes& bytes,
                       size_t chunk_bytes);

}

#endif
