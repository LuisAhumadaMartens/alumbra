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
#include "absl/status/statusor.h"
#include "demux_fuzz_support.h"
#include "fuzz_target.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/demux/demuxer.h"
#include "umbra/demux/ts_demuxer.h"

namespace {

constexpr std::size_t kMaxSteps = 200000;
constexpr std::size_t kMaxFetchedBytes = std::size_t{1} << 26;

void Check(bool condition) {
  if (!condition) {
    std::abort();
  }
}

class CountingOutput final : public umbra::demux::DemuxerOutput {
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
    Check(!sample.data.empty());
    ++samples;
  }
  void OnTimedMetadata(umbra::demux::TimedMetadataItem item) override {
    Check(item.stream_id != 0);
  }
  void OnProducerReferenceTime(
      const umbra::demux::ProducerReferenceTime& reference) override {
    Check(reference.media_time.is_known());
  }

  std::shared_ptr<const umbra::demux::SeekMap> seek_map;
  std::size_t samples = 0;
};

void CheckError(const absl::Status& status) {
  Check(status.code() == absl::StatusCode::kDataLoss ||
        status.code() == absl::StatusCode::kUnimplemented);
}

bool Drive(umbra::demux::Demuxer& demuxer, std::span<const std::uint8_t> input,
           std::size_t chunk) {
  std::size_t fetched = 0;
  for (std::size_t step_count = 0; step_count < kMaxSteps; ++step_count) {
    const absl::StatusOr<umbra::demux::StepResult> step = demuxer.Step();
    if (!step.ok()) {
      CheckError(step.status());
      return false;
    }
    if (step->kind == umbra::demux::StepKind::kEndOfStream) {
      return true;
    }
    if (step->kind != umbra::demux::StepKind::kNeedMoreData) {
      continue;
    }
    const std::uint64_t offset = demuxer.NextOffset();
    if (offset >= input.size() || fetched > kMaxFetchedBytes) {
      demuxer.EndOfInput();
      continue;
    }
    const std::size_t count = std::min<std::size_t>(
        chunk, input.size() - static_cast<std::size_t>(offset));
    fetched += count;
    Check(demuxer
              .Append(offset, penumbra::SharedBytes::Copy(input.subspan(
                                  static_cast<std::size_t>(offset), count)))
              .ok());
  }
  return false;
}

}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  if (size == 0) {
    return 0;
  }
  const alumbra::fuzz::DemuxInput fuzz_input =
      alumbra::fuzz::ConsumeDemuxInput(data, size);
  const std::uint8_t selector = fuzz_input.selector;
  const std::span<const std::uint8_t> body(fuzz_input.file_bytes);
  static constexpr std::array<std::size_t, 5> kChunks = {1, 7, 64, 1500, 65536};
  const std::size_t chunk = kChunks.at(selector % kChunks.size());
  umbra::demux::TsDemuxerOptions options;
  options.extract_captions = (selector & 0x08) == 0;
  options.probe_duration = (selector & 0x10) == 0;
  options.program_number = static_cast<std::uint32_t>(selector >> 5U) & 3U;
  CountingOutput output;
  umbra::demux::TsDemuxer demuxer(output, options);
  demuxer.SetSourceInfo(body.size(), true);
  const bool finished = Drive(demuxer, body, chunk);
  if (finished && output.seek_map != nullptr && output.seek_map->IsSeekable()) {
    const auto seconds = static_cast<std::int64_t>(selector >> 5);
    const auto point = output.seek_map->SeekPointFor(
        penumbra::MediaTime::FromMilliseconds(seconds * 700));
    if (point.has_value()) {
      const absl::StatusOr<std::uint64_t> offset = demuxer.Seek(*point);
      if (offset.ok()) {
        Check(demuxer.NextOffset() == *offset);
        Drive(demuxer, body, chunk);
      } else {
        CheckError(offset.status());
      }
    }
    for (const std::uint8_t operation : fuzz_input.seek_operations) {
      const auto target =
          output.seek_map->SeekPointFor(penumbra::MediaTime::FromMilliseconds(
              static_cast<std::int64_t>(operation) * 1000));
      if (!target.has_value()) {
        continue;
      }
      const absl::StatusOr<std::uint64_t> offset = demuxer.Seek(*target);
      if (offset.ok()) {
        Check(demuxer.NextOffset() == *offset);
        Drive(demuxer, body, chunk);
      } else {
        CheckError(offset.status());
      }
    }
  }
  return 0;
}
