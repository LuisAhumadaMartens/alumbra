#include "umbra/demux/ebml.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>

#include "absl/status/statusor.h"
#include "umbra/codec/reader_util.h"

namespace umbra::demux {
namespace {

size_t VintLength(uint8_t first) {
  return static_cast<size_t>(std::countl_zero(first)) + 1;
}

}

absl::StatusOr<size_t> EbmlHeaderBytes(EbmlBytes prefix) {
  if (prefix.empty()) {
    return size_t{1};
  }
  if (prefix[0] == 0) {
    return codec::Corrupt("EBML element id is invalid");
  }
  const size_t id_length = VintLength(prefix[0]);
  if (id_length > kMaxEbmlIdBytes) {
    return codec::Corrupt("EBML element id is longer than four bytes");
  }
  if (prefix.size() <= id_length) {
    return id_length + 1;
  }
  if (prefix[id_length] == 0) {
    return codec::Corrupt("EBML element size is longer than eight bytes");
  }
  return id_length + VintLength(prefix[id_length]);
}

absl::StatusOr<EbmlHeader> ParseEbmlHeader(EbmlBytes prefix) {
  const absl::StatusOr<size_t> needed = EbmlHeaderBytes(prefix);
  if (!needed.ok()) {
    return needed.status();
  }
  if (prefix.size() < *needed) {
    return codec::Corrupt("EBML element header is truncated");
  }
  const size_t id_length = VintLength(prefix[0]);
  EbmlHeader header;
  for (size_t index = 0; index < id_length; ++index) {
    header.id = (header.id << 8) | prefix[index];
  }
  const absl::StatusOr<Vint> size = ParseVint(prefix.subspan(id_length));
  if (!size.ok()) {
    return size.status();
  }
  header.size = size->all_ones ? kEbmlUnknownSize : size->value;
  header.header_size = id_length + size->length;
  return header;
}

absl::StatusOr<Vint> ParseVint(EbmlBytes data) {
  if (data.empty() || data[0] == 0) {
    return codec::Corrupt("EBML variable-length integer is invalid");
  }
  const size_t length = VintLength(data[0]);
  if (data.size() < length) {
    return codec::Corrupt("EBML variable-length integer is truncated");
  }
  const uint64_t mask = 0xFFU >> length;
  uint64_t value = data[0] & mask;
  bool all_ones = value == mask;
  for (size_t index = 1; index < length; ++index) {
    value = (value << 8) | data[index];
    all_ones = all_ones && data[index] == 0xFF;
  }
  Vint result;
  result.value = value;
  result.length = length;
  result.all_ones = all_ones;
  return result;
}

absl::StatusOr<EbmlElement> EbmlReader::Next() {
  const EbmlBytes rest = data_.subspan(position_);
  const absl::StatusOr<EbmlHeader> header = ParseEbmlHeader(rest);
  if (!header.ok()) {
    return header.status();
  }
  if (header->UnknownSize()) {
    return codec::Corrupt("EBML element has an unknown size inside a master");
  }
  const size_t available = rest.size() - header->header_size;
  if (header->size > available) {
    return codec::Corrupt("EBML element is larger than its parent");
  }
  EbmlElement element;
  element.id = header->id;
  element.payload =
      rest.subspan(header->header_size, static_cast<size_t>(header->size));
  position_ += header->header_size + static_cast<size_t>(header->size);
  return element;
}

absl::StatusOr<uint64_t> ReadEbmlUnsigned(EbmlBytes payload) {
  if (payload.size() > 8) {
    return codec::Corrupt("EBML unsigned integer is longer than eight bytes");
  }
  uint64_t value = 0;
  for (const uint8_t byte : payload) {
    value = (value << 8) | byte;
  }
  return value;
}

absl::StatusOr<int64_t> ReadEbmlSigned(EbmlBytes payload) {
  if (payload.size() > 8) {
    return codec::Corrupt("EBML signed integer is longer than eight bytes");
  }
  if (payload.empty()) {
    return int64_t{0};
  }
  uint64_t value = (payload[0] & 0x80U) != 0 ? ~uint64_t{0} : 0;
  for (const uint8_t byte : payload) {
    value = (value << 8) | byte;
  }
  return static_cast<int64_t>(value);
}

absl::StatusOr<double> ReadEbmlFloat(EbmlBytes payload) {
  if (payload.empty()) {
    return 0.0;
  }
  if (payload.size() != 4 && payload.size() != 8) {
    return codec::Corrupt("EBML float is not four or eight bytes");
  }
  uint64_t bits = 0;
  for (const uint8_t byte : payload) {
    bits = (bits << 8) | byte;
  }
  if (payload.size() == 4) {
    return static_cast<double>(
        std::bit_cast<float>(static_cast<uint32_t>(bits)));
  }
  return std::bit_cast<double>(bits);
}

absl::StatusOr<std::string> ReadEbmlString(EbmlBytes payload) {
  if (payload.size() > kMaxEbmlStringBytes) {
    return codec::Corrupt("EBML string is too long");
  }
  std::string text;
  for (const uint8_t byte : payload) {
    if (byte == 0) {
      break;
    }
    text.push_back(static_cast<char>(byte));
  }
  return text;
}

}
