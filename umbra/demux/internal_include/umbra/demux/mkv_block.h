#ifndef UMBRA_DEMUX_MKV_BLOCK_H_
#define UMBRA_DEMUX_MKV_BLOCK_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "absl/status/statusor.h"
#include "umbra/demux/ebml.h"

namespace umbra::demux {

inline constexpr size_t kMaxLacedFrames = 256;

enum class Lacing : uint8_t {
  kNone,
  kXiph,
  kFixed,
  kEbml,
};

struct FrameRange {
  size_t offset = 0;
  size_t size = 0;
};

struct MkvBlock {
  uint64_t track_number = 0;
  int16_t relative_ticks = 0;
  bool key_frame = false;
  bool invisible = false;
  bool discardable = false;
  Lacing lacing = Lacing::kNone;
  std::vector<FrameRange> frames;
};

[[nodiscard]] absl::StatusOr<MkvBlock> ParseBlock(EbmlBytes block);

}

#endif
