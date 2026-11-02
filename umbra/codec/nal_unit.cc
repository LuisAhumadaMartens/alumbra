#include "umbra/codec/nal_unit.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "umbra/base/status.h"
#include "umbra/codec/reader_util.h"

namespace umbra::codec {
namespace {

constexpr std::size_t kMaxLengthSize = 4;

absl::Status CheckLengthSize(std::size_t length_size) {
  if (length_size < 1 || length_size > kMaxLengthSize) {
    return absl::InvalidArgumentError(
        absl::StrCat("NAL length field of ", length_size, " bytes"));
  }
  return absl::OkStatus();
}

bool IsStartCode(Bytes data, std::size_t position) {
  return position + 3 <= data.size() && data[position] == 0 &&
         data[position + 1] == 0 && data[position + 2] == 1;
}

}

std::vector<std::uint8_t> RemoveEmulationPrevention(Bytes data) {
  std::vector<std::uint8_t> output;
  output.reserve(data.size());
  std::size_t zeros = 0;
  for (const std::uint8_t byte : data) {
    if (zeros >= 2 && byte == 3) {
      zeros = 0;
      continue;
    }
    zeros = byte == 0 ? zeros + 1 : 0;
    output.push_back(byte);
  }
  return output;
}

absl::StatusOr<std::vector<Bytes>> SplitAnnexB(Bytes data) {
  std::size_t position = 0;
  while (position < data.size() && !IsStartCode(data, position)) {
    if (data[position] != 0) {
      return Corrupt("Annex B stream does not begin with a start code");
    }
    ++position;
  }
  if (position >= data.size()) {
    return Corrupt("Annex B stream has no start code");
  }
  std::vector<Bytes> units;
  position += 3;
  while (position <= data.size()) {
    std::size_t end = position;
    while (end < data.size() && !IsStartCode(data, end)) {
      ++end;
    }
    std::size_t trimmed = end;
    while (trimmed > position && data[trimmed - 1] == 0) {
      --trimmed;
    }
    if (trimmed > position) {
      units.push_back(data.subspan(position, trimmed - position));
    }
    position = end + 3;
  }
  return units;
}

absl::StatusOr<std::vector<Bytes>> SplitLengthPrefixed(
    Bytes data, std::size_t length_size) {
  ALUMBRA_RETURN_IF_ERROR(CheckLengthSize(length_size));
  RecordReader reader(data, "length-prefixed NAL units");
  std::vector<Bytes> units;
  while (reader.remaining() > 0) {
    const absl::StatusOr<std::uint64_t> length = reader.Unsigned(length_size);
    if (!length.ok()) {
      return length.status();
    }
    const absl::StatusOr<Bytes> unit = reader.Take(*length);
    if (!unit.ok()) {
      return unit.status();
    }
    if (!unit->empty()) {
      units.push_back(*unit);
    }
  }
  return units;
}

absl::StatusOr<std::vector<std::uint8_t>> AnnexBToLengthPrefixed(
    Bytes data, std::size_t length_size) {
  ALUMBRA_RETURN_IF_ERROR(CheckLengthSize(length_size));
  const absl::StatusOr<std::vector<Bytes>> units = SplitAnnexB(data);
  if (!units.ok()) {
    return units.status();
  }
  const std::uint64_t limit = (std::uint64_t{1} << (8 * length_size)) - 1;
  std::vector<std::uint8_t> output;
  for (const Bytes unit : *units) {
    if (unit.size() > limit) {
      return Corrupt("NAL unit is too large for its length field");
    }
    for (std::size_t i = length_size; i > 0; --i) {
      output.push_back(static_cast<std::uint8_t>(unit.size() >> (8 * (i - 1))));
    }
    output.insert(output.end(), unit.begin(), unit.end());
  }
  return output;
}

absl::StatusOr<std::vector<std::uint8_t>> LengthPrefixedToAnnexB(
    Bytes data, std::size_t length_size) {
  const absl::StatusOr<std::vector<Bytes>> units =
      SplitLengthPrefixed(data, length_size);
  if (!units.ok()) {
    return units.status();
  }
  std::vector<std::uint8_t> output;
  for (const Bytes unit : *units) {
    output.insert(output.end(), {0, 0, 0, 1});
    output.insert(output.end(), unit.begin(), unit.end());
  }
  return output;
}

}
