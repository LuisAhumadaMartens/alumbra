#include "test_support.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "mp4_builder.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/demux/demuxer.h"

namespace umbra::demux::test {

void RecordingOutput::OnStreams(std::vector<penumbra::StreamInfo> declared) {
  streams = std::move(declared);
  ++stream_announcements;
}

void RecordingOutput::OnMetadataStream(const MetadataStreamInfo& stream) {
  metadata_streams.push_back(stream);
}

void RecordingOutput::OnSeekMap(std::shared_ptr<const SeekMap> map) {
  seek_map = std::move(map);
  ++seek_map_count;
}

void RecordingOutput::OnSample(penumbra::EncodedSample sample) {
  samples.push_back(std::move(sample));
}

void RecordingOutput::OnTimedMetadata(TimedMetadataItem item) {
  items.push_back(std::move(item));
}

void RecordingOutput::OnProducerReferenceTime(
    const ProducerReferenceTime& reference) {
  references.push_back(reference);
}

std::vector<penumbra::EncodedSample> RecordingOutput::SamplesOf(
    uint32_t track_id) const {
  std::vector<penumbra::EncodedSample> result;
  for (const penumbra::EncodedSample& sample : samples) {
    if (sample.track_id == track_id) {
      result.push_back(sample);
    }
  }
  return result;
}

namespace {

template <typename Reader>
DriveResult DriveWith(Demuxer& demuxer, uint64_t size, size_t chunk_bytes,
                      const Reader& read, StopAfter stop) {
  DriveResult result;
  demuxer.SetSourceInfo(size, true);
  constexpr size_t kMaxSteps = 20000000;
  while (result.steps < kMaxSteps) {
    ++result.steps;
    const absl::StatusOr<StepResult> step = demuxer.Step();
    if (!step.ok()) {
      result.status = step.status();
      return result;
    }
    if (step->kind == StepKind::kEndOfStream) {
      return result;
    }
    if (stop.output != nullptr && stop.output->samples.size() >= stop.samples) {
      return result;
    }
    if (step->kind == StepKind::kSeek) {
      ++result.seek_requests;
      continue;
    }
    if (step->kind == StepKind::kYield) {
      continue;
    }
    const uint64_t offset = demuxer.NextOffset();
    const Bytes data = read(offset, chunk_bytes);
    if (data.empty()) {
      demuxer.EndOfInput();
      continue;
    }
    result.bytes_fetched += data.size();
    const absl::Status appended =
        demuxer.Append(offset, penumbra::SharedBytes::Copy(data));
    if (!appended.ok()) {
      result.status = appended;
      return result;
    }
  }
  result.status = absl::InternalError("the demuxer did not finish");
  return result;
}

}

DriveResult Drive(Demuxer& demuxer, const BuiltFile& file, size_t chunk_bytes,
                  StopAfter stop) {
  return DriveWith(
      demuxer, file.size, chunk_bytes,
      [&file](uint64_t offset, size_t size) { return file.Read(offset, size); },
      stop);
}

DriveResult DriveBytes(Demuxer& demuxer, const Bytes& bytes,
                       size_t chunk_bytes) {
  return DriveWith(
      demuxer, bytes.size(), chunk_bytes,
      [&bytes](uint64_t offset, size_t size) {
        if (offset >= bytes.size()) {
          return Bytes();
        }
        const auto available = static_cast<size_t>(bytes.size() - offset);
        const auto begin = bytes.begin() + static_cast<std::ptrdiff_t>(offset);
        return Bytes(begin, begin + static_cast<std::ptrdiff_t>(
                                        std::min(size, available)));
      },
      StopAfter{});
}

}
