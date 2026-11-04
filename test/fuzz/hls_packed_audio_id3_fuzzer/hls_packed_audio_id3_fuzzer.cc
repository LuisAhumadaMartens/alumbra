#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <span>
#include <vector>

#include "absl/status/status.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/demux/demuxer.h"
#include "umbra/demux/packed_audio_demuxer.h"

namespace {

class FuzzOutput final : public umbra::demux::DemuxerOutput {
 public:
  void OnStreams(std::vector<penumbra::StreamInfo> streams) override {
    for (const penumbra::StreamInfo& stream : streams) {
      if (!stream.time_base.is_positive()) {
        std::abort();
      }
    }
  }
  void OnMetadataStream(
      const umbra::demux::MetadataStreamInfo& stream) override {
    if (stream.stream_id == 0) {
      std::abort();
    }
  }
  void OnSeekMap(
      std::shared_ptr<const umbra::demux::SeekMap> seek_map) override {
    static_cast<void>(seek_map);
  }
  void OnSample(penumbra::EncodedSample sample) override {
    if (!sample.pts.is_known() || !sample.dts.is_known() ||
        !sample.duration.is_known()) {
      std::abort();
    }
  }
  void OnTimedMetadata(umbra::demux::TimedMetadataItem item) override {
    if (item.stream_id == 0) {
      std::abort();
    }
  }
  void OnProducerReferenceTime(
      const umbra::demux::ProducerReferenceTime& reference) override {
    static_cast<void>(reference);
  }
};

}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  FuzzOutput output;
  umbra::demux::Mp3Demuxer demuxer(output);
  demuxer.SetSourceInfo(size, true);
  const std::span<const std::uint8_t> input(data, size);
  std::size_t offset = 0;
  for (std::size_t step_count = 0; step_count < 200000; ++step_count) {
    const auto step = demuxer.Step();
    if (!step.ok()) {
      if (step.status().code() != absl::StatusCode::kDataLoss) {
        std::abort();
      }
      return 0;
    }
    if (step->kind == umbra::demux::StepKind::kEndOfStream) {
      return 0;
    }
    if (step->kind == umbra::demux::StepKind::kNeedMoreData) {
      if (offset == size) {
        demuxer.EndOfInput();
        continue;
      }
      const std::size_t count = std::min<std::size_t>(1500, size - offset);
      if (!demuxer
               .Append(offset, penumbra::SharedBytes::Copy(
                                   input.subspan(offset, count)))
               .ok()) {
        std::abort();
      }
      offset += count;
    }
  }
  return 0;
}
