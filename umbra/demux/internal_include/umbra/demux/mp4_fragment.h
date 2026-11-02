#ifndef UMBRA_DEMUX_MP4_FRAGMENT_H_
#define UMBRA_DEMUX_MP4_FRAGMENT_H_

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "umbra/demux/mp4_types.h"

namespace umbra::demux {

inline constexpr uint32_t kSampleIsNonSync = 0x10000;

struct TrackFragment {
  uint32_t track_id = 0;
  bool has_decode_time = false;
  int64_t first_dts = 0;
  SampleList samples;
};

struct Fragment {
  uint64_t moof_offset = 0;
  uint64_t moof_size = 0;
  std::vector<TrackFragment> tracks;
};

struct FragmentContext {
  const std::vector<TrackDef>* tracks = nullptr;
  const std::vector<std::optional<int64_t>>* expected_dts = nullptr;
  uint64_t sample_budget = 0;
};

struct SidxReference {
  bool hierarchical = false;
  uint32_t size = 0;
  uint32_t duration = 0;
  bool starts_with_sap = false;
};

struct Sidx {
  uint32_t reference_id = 0;
  uint32_t timescale = 0;
  uint64_t earliest_presentation_time = 0;
  uint64_t first_offset = 0;
  std::vector<SidxReference> references;
};

struct Emsg {
  uint8_t version = 0;
  std::string scheme;
  std::string value;
  uint32_t timescale = 0;
  uint64_t time = 0;
  uint32_t event_duration = 0;
  uint32_t id = 0;
  std::vector<uint8_t> message;
};

struct Prft {
  uint32_t track_id = 0;
  uint64_t ntp_timestamp = 0;
  uint64_t media_time = 0;
  uint32_t flags = 0;
};

[[nodiscard]] absl::StatusOr<Fragment> ParseMoof(
    std::span<const uint8_t> payload, uint64_t moof_offset, uint64_t moof_size,
    const FragmentContext& context);

[[nodiscard]] absl::StatusOr<Sidx> ParseSidx(std::span<const uint8_t> payload);

[[nodiscard]] absl::StatusOr<Emsg> ParseEmsg(std::span<const uint8_t> payload);

[[nodiscard]] absl::StatusOr<Prft> ParsePrft(std::span<const uint8_t> payload);

}

#endif
