#ifndef UMBRA_DEMUX_MP4_SAMPLE_ENTRY_H_
#define UMBRA_DEMUX_MP4_SAMPLE_ENTRY_H_

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "penumbra/interface/rational.h"
#include "umbra/demux/mp4_types.h"

namespace umbra::demux {

struct EsdsInfo {
  uint8_t object_type_indication = 0;
  std::vector<uint8_t> decoder_specific_info;
};

struct SampleEntryContext {
  uint32_t track_id = 0;
  penumbra::Rational time_base;
  std::string language;
  uint32_t handler = 0;
};

[[nodiscard]] absl::StatusOr<EsdsInfo> ParseEsds(
    std::span<const uint8_t> payload);

[[nodiscard]] absl::StatusOr<SampleEntryInfo> ParseSampleEntry(
    uint32_t fourcc, std::span<const uint8_t> payload,
    const SampleEntryContext& context);

}

#endif
