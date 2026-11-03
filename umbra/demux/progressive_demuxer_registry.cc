#include "umbra/demux/progressive_demuxer_registry.h"

#include <array>
#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "umbra/demux/demuxer.h"
#include "umbra/demux/mp4_demuxer.h"
#include "umbra/demux/packed_audio_demuxer.h"
#include "umbra/source/source_type.h"

namespace umbra::demux {
namespace {

using Factory = std::unique_ptr<Demuxer> (*)(DemuxerOutput&);

struct Entry {
  source::SourceType type;
  Factory factory;
};

std::unique_ptr<Demuxer> CreateMp4(DemuxerOutput& output) {
  return std::make_unique<Mp4Demuxer>(output);
}

std::unique_ptr<Demuxer> CreateAdts(DemuxerOutput& output) {
  return std::make_unique<AdtsDemuxer>(output);
}

std::unique_ptr<Demuxer> CreateMp3(DemuxerOutput& output) {
  return std::make_unique<Mp3Demuxer>(output);
}

constexpr std::array<Entry, 3> kRegistry = {
    {{.type = source::SourceType::kMp4, .factory = &CreateMp4},
     {.type = source::SourceType::kAdts, .factory = &CreateAdts},
     {.type = source::SourceType::kMp3, .factory = &CreateMp3}}};

}

absl::StatusOr<std::unique_ptr<Demuxer>> CreateProgressiveDemuxer(
    source::SourceType type, DemuxerOutput& output) {
  for (const Entry& entry : kRegistry) {
    if (entry.type == type) {
      return entry.factory(output);
    }
  }
  return absl::UnimplementedError("source not supported");
}

}
