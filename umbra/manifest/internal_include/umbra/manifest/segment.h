#ifndef UMBRA_MANIFEST_SEGMENT_H_
#define UMBRA_MANIFEST_SEGMENT_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "absl/time/time.h"
#include "penumbra/interface/media_time.h"

namespace umbra::manifest {

struct ByteRange {
  std::int64_t offset = 0;
  std::int64_t length = 0;

  friend bool operator==(const ByteRange&, const ByteRange&) = default;
};

struct InitSection {
  std::string uri;
  std::optional<ByteRange> byte_range;
  std::optional<std::uint32_t> key_index;

  friend bool operator==(const InitSection&, const InitSection&) = default;
};

enum class KeyMethod : std::uint8_t {
  kAes128,
  kSampleAes,
  kSampleAesCtr,
};

struct KeyReference {
  KeyMethod method = KeyMethod::kAes128;
  std::string uri;
  std::vector<std::uint8_t> iv;
  std::string key_format;

  friend bool operator==(const KeyReference&, const KeyReference&) = default;
};

struct HlsPart {
  std::string uri;
  penumbra::TimeDelta duration;
  std::optional<ByteRange> byte_range;
  bool independent = false;
  bool gap = false;

  friend bool operator==(const HlsPart&, const HlsPart&) = default;
};

struct Segment {
  std::int64_t sequence_number = 0;
  penumbra::MediaTime start;
  penumbra::TimeDelta duration;
  std::string uri;
  std::optional<ByteRange> byte_range;
  std::optional<std::uint32_t> init_index;
  std::optional<std::uint32_t> key_index;
  std::int64_t discontinuity_sequence = 0;
  bool gap = false;
  std::optional<std::int64_t> bitrate_kbps;
  std::optional<absl::Time> program_date_time;
  std::vector<HlsPart> hls_parts;

  [[nodiscard]] penumbra::MediaTime End() const { return start + duration; }

  friend bool operator==(const Segment&, const Segment&) = default;
};

}

#endif
