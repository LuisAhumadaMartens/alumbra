#ifndef UMBRA_DEMUX_MP4_BOX_H_
#define UMBRA_DEMUX_MP4_BOX_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "absl/status/statusor.h"
#include "umbra/demux/mp4_types.h"

namespace umbra::demux {

inline constexpr uint32_t kBoxFtyp = FourCc('f', 't', 'y', 'p');
inline constexpr uint32_t kBoxStyp = FourCc('s', 't', 'y', 'p');
inline constexpr uint32_t kBoxMoov = FourCc('m', 'o', 'o', 'v');
inline constexpr uint32_t kBoxMoof = FourCc('m', 'o', 'o', 'f');
inline constexpr uint32_t kBoxMdat = FourCc('m', 'd', 'a', 't');
inline constexpr uint32_t kBoxSidx = FourCc('s', 'i', 'd', 'x');
inline constexpr uint32_t kBoxEmsg = FourCc('e', 'm', 's', 'g');
inline constexpr uint32_t kBoxPrft = FourCc('p', 'r', 'f', 't');
inline constexpr uint32_t kBoxUuid = FourCc('u', 'u', 'i', 'd');
inline constexpr uint32_t kBoxFree = FourCc('f', 'r', 'e', 'e');
inline constexpr uint32_t kBoxSkip = FourCc('s', 'k', 'i', 'p');
inline constexpr uint32_t kBoxWide = FourCc('w', 'i', 'd', 'e');
inline constexpr uint32_t kBoxPdin = FourCc('p', 'd', 'i', 'n');
inline constexpr uint32_t kBoxMeta = FourCc('m', 'e', 't', 'a');

inline constexpr size_t kMinBoxHeaderBytes = 8;
inline constexpr size_t kMaxBoxHeaderBytes = 32;

struct BoxHeader {
  uint32_t type = 0;
  uint64_t total_size = 0;
  size_t header_size = 0;
};

struct ChildBox {
  uint32_t type = 0;
  std::span<const uint8_t> payload;
};

[[nodiscard]] size_t BoxHeaderBytes(std::span<const uint8_t> prefix);

[[nodiscard]] absl::StatusOr<BoxHeader> ParseBoxHeader(
    std::span<const uint8_t> bytes);

[[nodiscard]] absl::StatusOr<std::vector<ChildBox>> SplitChildren(
    std::span<const uint8_t> payload, size_t depth);

[[nodiscard]] bool IsKnownTopLevelBox(uint32_t type);

}

#endif
