#ifndef UMBRA_DEMUX_MP4_TABLES_H_
#define UMBRA_DEMUX_MP4_TABLES_H_

#include <cstdint>
#include <memory>
#include <span>

#include "absl/status/statusor.h"
#include "umbra/demux/mp4_types.h"

namespace umbra::demux {

[[nodiscard]] absl::StatusOr<Movie> ParseMovie(
    std::span<const uint8_t> moov_payload);

[[nodiscard]] absl::StatusOr<std::shared_ptr<const SampleList>> BuildSampleList(
    const RawSampleTable& table, uint64_t sample_budget);

[[nodiscard]] TrackTiming ComputeTiming(const TrackDef& track,
                                        uint32_t movie_timescale,
                                        bool edit_lists_enabled);

}

#endif
