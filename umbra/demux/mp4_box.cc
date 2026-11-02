#include "umbra/demux/mp4_box.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "umbra/codec/reader_util.h"
#include "umbra/demux/mp4_types.h"

namespace umbra::demux {
namespace {

constexpr uint32_t kLargeSizeMarker = 1;
constexpr size_t kUuidBytes = 16;

uint32_t ReadBigEndian32(std::span<const uint8_t> bytes) {
  return (static_cast<uint32_t>(bytes[0]) << 24) |
         (static_cast<uint32_t>(bytes[1]) << 16) |
         (static_cast<uint32_t>(bytes[2]) << 8) |
         static_cast<uint32_t>(bytes[3]);
}

uint64_t ReadBigEndian64(std::span<const uint8_t> bytes) {
  return (static_cast<uint64_t>(ReadBigEndian32(bytes)) << 32) |
         ReadBigEndian32(bytes.subspan(4));
}

}

size_t BoxHeaderBytes(std::span<const uint8_t> prefix) {
  if (prefix.size() < kMinBoxHeaderBytes) {
    return kMinBoxHeaderBytes;
  }
  size_t bytes = kMinBoxHeaderBytes;
  if (ReadBigEndian32(prefix) == kLargeSizeMarker) {
    bytes += sizeof(uint64_t);
  }
  if (ReadBigEndian32(prefix.subspan(4)) == kBoxUuid) {
    bytes += kUuidBytes;
  }
  return bytes;
}

absl::StatusOr<BoxHeader> ParseBoxHeader(std::span<const uint8_t> bytes) {
  if (bytes.size() < BoxHeaderBytes(bytes)) {
    return codec::Corrupt("box header is truncated");
  }
  BoxHeader header;
  const uint32_t size32 = ReadBigEndian32(bytes);
  header.type = ReadBigEndian32(bytes.subspan(4));
  header.header_size = kMinBoxHeaderBytes;
  header.total_size = size32;
  if (size32 == kLargeSizeMarker) {
    header.total_size = ReadBigEndian64(bytes.subspan(kMinBoxHeaderBytes));
    header.header_size += sizeof(uint64_t);
  }
  if (header.type == kBoxUuid) {
    header.header_size += kUuidBytes;
  }
  const bool to_end = size32 == 0;
  if (to_end) {
    header.total_size = 0;
    return header;
  }
  if (header.total_size < header.header_size) {
    return codec::Corrupt(absl::StrCat("box size ", header.total_size,
                                       " is smaller than its header"));
  }
  return header;
}

absl::StatusOr<std::vector<ChildBox>> SplitChildren(
    std::span<const uint8_t> payload, size_t depth) {
  if (depth > kMaxBoxDepth) {
    return codec::Corrupt("boxes are nested too deeply");
  }
  std::vector<ChildBox> children;
  while (!payload.empty()) {
    if (payload.size() < kMinBoxHeaderBytes ||
        payload.size() < BoxHeaderBytes(payload)) {
      return codec::Corrupt("child box header is truncated");
    }
    const absl::StatusOr<BoxHeader> header = ParseBoxHeader(payload);
    if (!header.ok()) {
      return header.status();
    }
    uint64_t total = header->total_size;
    if (total == 0) {
      total = payload.size();
    }
    if (total > payload.size()) {
      return codec::Corrupt(absl::StrCat("child box of ", total,
                                         " bytes exceeds its parent of ",
                                         payload.size(), " bytes"));
    }
    children.push_back(ChildBox{
        .type = header->type,
        .payload =
            payload.subspan(header->header_size, total - header->header_size)});
    payload = payload.subspan(static_cast<size_t>(total));
  }
  return children;
}

bool IsKnownTopLevelBox(uint32_t type) {
  return type == kBoxFtyp || type == kBoxStyp || type == kBoxMoov ||
         type == kBoxMoof || type == kBoxMdat || type == kBoxSidx ||
         type == kBoxEmsg || type == kBoxPrft || type == kBoxFree ||
         type == kBoxSkip || type == kBoxWide || type == kBoxPdin ||
         type == kBoxMeta || type == kBoxUuid;
}

}
