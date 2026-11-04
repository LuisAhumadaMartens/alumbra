#include "umbra/demux/mkv_block.h"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "umbra/base/status.h"
#include "umbra/codec/reader_util.h"
#include "umbra/demux/ebml.h"

namespace umbra::demux {
namespace {

constexpr uint8_t kKeyFrameFlag = 0x80;
constexpr uint8_t kInvisibleFlag = 0x08;
constexpr uint8_t kLacingMask = 0x06;
constexpr uint8_t kDiscardableFlag = 0x01;
constexpr uint8_t kLacingXiph = 0x02;
constexpr uint8_t kLacingFixed = 0x04;
constexpr uint8_t kLacingEbml = 0x06;

Lacing LacingOf(uint8_t flags) {
  const uint8_t bits = flags & kLacingMask;
  if (bits == kLacingXiph) {
    return Lacing::kXiph;
  }
  if (bits == kLacingFixed) {
    return Lacing::kFixed;
  }
  if (bits == kLacingEbml) {
    return Lacing::kEbml;
  }
  return Lacing::kNone;
}

absl::StatusOr<std::vector<size_t>> XiphSizes(EbmlBytes data, size_t& position,
                                              size_t count) {
  std::vector<size_t> sizes;
  for (size_t index = 0; index + 1 < count; ++index) {
    size_t size = 0;
    for (;;) {
      if (position >= data.size()) {
        return codec::Corrupt("Xiph lacing is truncated");
      }
      const uint8_t byte = data[position++];
      size += byte;
      if (byte != 0xFF) {
        break;
      }
    }
    sizes.push_back(size);
  }
  return sizes;
}

absl::StatusOr<std::vector<size_t>> EbmlSizes(EbmlBytes data, size_t& position,
                                              size_t count) {
  std::vector<size_t> sizes;
  if (count < 2) {
    return sizes;
  }
  ALUMBRA_ASSIGN_OR_RETURN(const Vint first, ParseVint(data.subspan(position)));
  position += first.length;
  if (first.value > data.size()) {
    return codec::Corrupt("EBML lacing size is larger than the block");
  }
  sizes.push_back(static_cast<size_t>(first.value));
  for (size_t index = 2; index < count; ++index) {
    ALUMBRA_ASSIGN_OR_RETURN(const Vint raw, ParseVint(data.subspan(position)));
    position += raw.length;
    const auto bias =
        static_cast<int64_t>((uint64_t{1} << (7 * raw.length - 1)) - 1);
    const int64_t difference = static_cast<int64_t>(raw.value) - bias;
    const int64_t size = static_cast<int64_t>(sizes.back()) + difference;
    if (size < 0 || std::cmp_greater(size, data.size())) {
      return codec::Corrupt("EBML lacing size is out of range");
    }
    sizes.push_back(static_cast<size_t>(size));
  }
  return sizes;
}

absl::Status PlaceFrames(const std::vector<size_t>& sizes, size_t position,
                         size_t total, MkvBlock& block) {
  size_t offset = position;
  for (const size_t size : sizes) {
    if (size > total - offset) {
      return codec::Corrupt("laced frame is larger than the block");
    }
    block.frames.push_back(FrameRange{.offset = offset, .size = size});
    offset += size;
  }
  block.frames.push_back(FrameRange{.offset = offset, .size = total - offset});
  return absl::OkStatus();
}

}

absl::StatusOr<MkvBlock> ParseBlock(EbmlBytes block) {
  ALUMBRA_ASSIGN_OR_RETURN(const Vint track, ParseVint(block));
  if (track.all_ones || track.value == 0) {
    return codec::Corrupt("block has an invalid track number");
  }
  size_t position = track.length;
  if (block.size() < position + 3) {
    return codec::Corrupt("block header is truncated");
  }
  MkvBlock result;
  result.track_number = track.value;
  result.relative_ticks = static_cast<int16_t>(
      static_cast<uint16_t>((block[position] << 8) | block[position + 1]));
  const uint8_t flags = block[position + 2];
  position += 3;
  result.key_frame = (flags & kKeyFrameFlag) != 0;
  result.invisible = (flags & kInvisibleFlag) != 0;
  result.discardable = (flags & kDiscardableFlag) != 0;
  result.lacing = LacingOf(flags);
  if (result.lacing == Lacing::kNone) {
    result.frames.push_back(
        FrameRange{.offset = position, .size = block.size() - position});
    return result;
  }
  if (position >= block.size()) {
    return codec::Corrupt("laced block has no frame count");
  }
  const size_t count = static_cast<size_t>(block[position++]) + 1;
  if (count > kMaxLacedFrames) {
    return codec::Corrupt("laced block has too many frames");
  }
  if (result.lacing == Lacing::kFixed) {
    const size_t payload = block.size() - position;
    if (payload % count != 0) {
      return codec::Corrupt("fixed lacing does not divide the block");
    }
    for (size_t index = 0; index < count; ++index) {
      result.frames.push_back(
          FrameRange{.offset = position + (index * (payload / count)),
                     .size = payload / count});
    }
    return result;
  }
  std::vector<size_t> sizes;
  if (result.lacing == Lacing::kXiph) {
    ALUMBRA_ASSIGN_OR_RETURN(sizes, XiphSizes(block, position, count));
  } else {
    ALUMBRA_ASSIGN_OR_RETURN(sizes, EbmlSizes(block, position, count));
  }
  ALUMBRA_RETURN_IF_ERROR(PlaceFrames(sizes, position, block.size(), result));
  return result;
}

}
