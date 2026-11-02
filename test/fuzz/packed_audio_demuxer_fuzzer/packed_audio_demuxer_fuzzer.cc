#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "fuzz_target.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/demux/demuxer.h"
#include "umbra/demux/packed_audio_demuxer.h"

namespace {

void Check(bool condition) {
  if (!condition) {
    std::abort();
  }
}

class FuzzOutput final : public umbra::demux::DemuxerOutput {
 public:
  void OnStreams(std::vector<penumbra::StreamInfo> streams) override {
    for (const penumbra::StreamInfo& stream : streams) {
      Check(stream.time_base.is_positive());
    }
  }
  void OnMetadataStream(
      const umbra::demux::MetadataStreamInfo& stream) override {
    Check(stream.stream_id != 0);
  }
  void OnSeekMap(std::shared_ptr<const umbra::demux::SeekMap> map) override {
    seek_map = std::move(map);
  }
  void OnSample(penumbra::EncodedSample sample) override {
    Check(sample.pts.is_known());
    Check(sample.dts.is_known());
    Check(sample.duration.is_known());
  }
  void OnTimedMetadata(umbra::demux::TimedMetadataItem item) override {
    Check(item.stream_id != 0);
  }
  void OnProducerReferenceTime(
      const umbra::demux::ProducerReferenceTime& reference) override {
    static_cast<void>(reference);
  }

  std::shared_ptr<const umbra::demux::SeekMap> seek_map;
};

}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  if (size == 0) {
    return 0;
  }
  const std::span<const uint8_t> input(data, size);
  const bool adts = (input.front() & 1U) == 0;
  const std::array<std::size_t, 4> chunks = {1, 7, 64, 1500};
  const size_t chunk_size = chunks.at((input.front() >> 1) % chunks.size());
  const std::span<const uint8_t> bytes = input.subspan(1);
  FuzzOutput output;
  std::unique_ptr<umbra::demux::PackedAudioDemuxer> demuxer;
  if (adts) {
    demuxer = std::make_unique<umbra::demux::AdtsDemuxer>(output);
  } else {
    demuxer = std::make_unique<umbra::demux::Mp3Demuxer>(output);
  }
  demuxer->SetSourceInfo(bytes.size(), true);
  size_t offset = 0;
  for (size_t step_count = 0; step_count < 200000; ++step_count) {
    const auto step = demuxer->Step();
    if (!step.ok()) {
      Check(step.status().code() == absl::StatusCode::kDataLoss);
      return 0;
    }
    if (step->kind == umbra::demux::StepKind::kEndOfStream) {
      return 0;
    }
    if (step->kind == umbra::demux::StepKind::kNeedMoreData) {
      if (offset == bytes.size()) {
        demuxer->EndOfInput();
        continue;
      }
      const size_t count = std::min(chunk_size, bytes.size() - offset);
      Check(demuxer
                ->Append(offset, penumbra::SharedBytes::Copy(
                                     bytes.subspan(offset, count)))
                .ok());
      offset += count;
    }
  }
  return 0;
}
