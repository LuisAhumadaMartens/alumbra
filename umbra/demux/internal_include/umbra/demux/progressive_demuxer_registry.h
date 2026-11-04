#ifndef UMBRA_DEMUX_PROGRESSIVE_DEMUXER_REGISTRY_H_
#define UMBRA_DEMUX_PROGRESSIVE_DEMUXER_REGISTRY_H_

#include <memory>

#include "absl/status/statusor.h"
#include "umbra/demux/demuxer.h"
#include "umbra/source/source_type.h"

namespace umbra::demux {

[[nodiscard]] absl::StatusOr<std::unique_ptr<Demuxer>> CreateProgressiveDemuxer(
    source::SourceType type, DemuxerOutput& output);

}

#endif
