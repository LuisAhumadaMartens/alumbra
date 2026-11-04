#include "umbra/demux/caption_extractor.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "umbra/base/byte_reader.h"
#include "umbra/codec/nal_unit.h"

namespace umbra::demux {
namespace {

constexpr uint8_t kH264SeiNalType = 6;
constexpr uint8_t kHevcPrefixSeiNalType = 39;
constexpr uint8_t kHevcSuffixSeiNalType = 40;
constexpr uint8_t kHevcDolbyVisionRpuNalType = 62;
constexpr uint32_t kUserDataRegisteredItuTT35 = 4;
constexpr uint8_t kItuT35CountryUnitedStates = 0xB5;
constexpr uint16_t kItuT35ProviderAtsc = 0x0031;
constexpr uint32_t kAtscIdentifierGa94 = 0x47413934;
constexpr uint8_t kCcDataUserDataTypeCode = 0x03;
constexpr uint8_t kProcessCcDataFlag = 0x40;
constexpr uint8_t kCcCountMask = 0x1F;
constexpr uint16_t kItuT35ProviderHdr10Plus = 0x003C;
constexpr size_t kCaptionTripletBytes = 3;

absl::StatusOr<uint32_t> ReadSeiValue(base::ByteReader& reader) {
  uint32_t value = 0;
  while (true) {
    const absl::StatusOr<uint8_t> byte = reader.ReadU8();
    if (!byte.ok()) {
      return byte.status();
    }
    value += *byte;
    if (*byte != 0xFF) {
      return value;
    }
    if (value > 0x00FFFFFF) {
      return absl::OutOfRangeError("SEI value too large");
    }
  }
}

bool IsRegisteredCaptionPayload(base::ByteReader& reader) {
  const absl::StatusOr<uint8_t> country = reader.ReadU8();
  const absl::StatusOr<uint16_t> provider = reader.ReadU16();
  const absl::StatusOr<uint32_t> identifier = reader.ReadU32();
  const absl::StatusOr<uint8_t> type_code = reader.ReadU8();
  return country.ok() && provider.ok() && identifier.ok() && type_code.ok() &&
         *country == kItuT35CountryUnitedStates &&
         *provider == kItuT35ProviderAtsc &&
         *identifier == kAtscIdentifierGa94 &&
         *type_code == kCcDataUserDataTypeCode;
}

std::vector<uint8_t> ReadCaptionTriplets(base::ByteReader& reader) {
  const absl::StatusOr<uint8_t> flags = reader.ReadU8();
  const absl::StatusOr<uint8_t> em_data = reader.ReadU8();
  if (!flags.ok() || !em_data.ok() || (*flags & kProcessCcDataFlag) == 0) {
    return {};
  }
  const auto count = static_cast<size_t>(*flags & kCcCountMask);
  const absl::StatusOr<std::span<const uint8_t>> triplets =
      reader.ReadBytes(count * kCaptionTripletBytes);
  if (!triplets.ok() || count == 0) {
    return {};
  }
  return {triplets->begin(), triplets->end()};
}

std::vector<uint8_t> ParseCaptionMessage(std::span<const uint8_t> payload) {
  base::ByteReader reader(payload);
  if (!IsRegisteredCaptionPayload(reader)) {
    return {};
  }
  return ReadCaptionTriplets(reader);
}

bool IsHdr10PlusPayload(std::span<const uint8_t> payload) {
  return payload.size() >= 7 && payload[0] == kItuT35CountryUnitedStates &&
         (static_cast<uint16_t>(payload[1]) << 8 | payload[2]) ==
             kItuT35ProviderHdr10Plus &&
         payload[3] == 0 && payload[4] == 1 && payload[5] == 4;
}

bool IsSeiNal(CaptionCodec codec, std::span<const uint8_t> nal) {
  if (nal.empty()) {
    return false;
  }
  if (codec == CaptionCodec::kH264) {
    return (nal[0] & 0x1F) == kH264SeiNalType;
  }
  const auto type = static_cast<uint8_t>((nal[0] >> 1) & 0x3F);
  return type == kHevcPrefixSeiNalType || type == kHevcSuffixSeiNalType;
}

size_t NalHeaderSize(CaptionCodec codec) {
  return codec == CaptionCodec::kH264 ? 1 : 2;
}

}

std::vector<std::vector<uint8_t>> ExtractCaptionDataFromSei(
    std::span<const uint8_t> sei_rbsp) {
  std::vector<std::vector<uint8_t>> result;
  base::ByteReader reader(sei_rbsp);
  size_t messages = 0;
  while (reader.remaining() > 1 && messages < kMaxCaptionMessagesPerSample) {
    const absl::StatusOr<uint32_t> type = ReadSeiValue(reader);
    if (!type.ok()) {
      break;
    }
    const absl::StatusOr<uint32_t> size = ReadSeiValue(reader);
    if (!size.ok()) {
      break;
    }
    const absl::StatusOr<std::span<const uint8_t>> payload =
        reader.ReadBytes(*size);
    if (!payload.ok()) {
      break;
    }
    ++messages;
    if (*type != kUserDataRegisteredItuTT35) {
      continue;
    }
    std::vector<uint8_t> triplets = ParseCaptionMessage(*payload);
    if (!triplets.empty()) {
      result.push_back(std::move(triplets));
    }
  }
  return result;
}

std::vector<uint8_t> ExtractHdrDynamicMetadata(
    CaptionCodec codec, size_t nal_length_size,
    std::span<const uint8_t> sample) {
  const absl::StatusOr<std::vector<std::span<const uint8_t>>> units =
      codec::SplitLengthPrefixed(sample, nal_length_size);
  if (!units.ok()) {
    return {};
  }
  const size_t header_size = NalHeaderSize(codec);
  for (const std::span<const uint8_t>& nal : *units) {
    if (nal.size() <= header_size) {
      continue;
    }
    if (codec == CaptionCodec::kHevc &&
        ((nal[0] >> 1) & 0x3fU) == kHevcDolbyVisionRpuNalType) {
      return {nal.begin(), nal.end()};
    }
    if (!IsSeiNal(codec, nal)) {
      continue;
    }
    const std::vector<uint8_t> rbsp =
        codec::RemoveEmulationPrevention(nal.subspan(header_size));
    base::ByteReader reader(rbsp);
    size_t messages = 0;
    while (reader.remaining() > 1 && messages < kMaxCaptionMessagesPerSample) {
      const absl::StatusOr<uint32_t> type = ReadSeiValue(reader);
      const absl::StatusOr<uint32_t> size = ReadSeiValue(reader);
      if (!type.ok() || !size.ok()) {
        break;
      }
      const absl::StatusOr<std::span<const uint8_t>> payload =
          reader.ReadBytes(*size);
      if (!payload.ok()) {
        break;
      }
      ++messages;
      if (*type == kUserDataRegisteredItuTT35 && IsHdr10PlusPayload(*payload)) {
        return {payload->begin(), payload->end()};
      }
    }
  }
  return {};
}

std::vector<std::vector<uint8_t>> ExtractCaptionData(
    CaptionCodec codec, size_t nal_length_size,
    std::span<const uint8_t> sample) {
  std::vector<std::vector<uint8_t>> result;
  const absl::StatusOr<std::vector<std::span<const uint8_t>>> units =
      codec::SplitLengthPrefixed(sample, nal_length_size);
  if (!units.ok()) {
    return result;
  }
  const size_t header_size = NalHeaderSize(codec);
  for (const std::span<const uint8_t>& nal : *units) {
    if (!IsSeiNal(codec, nal) || nal.size() <= header_size) {
      continue;
    }
    const std::vector<uint8_t> rbsp =
        codec::RemoveEmulationPrevention(nal.subspan(header_size));
    for (std::vector<uint8_t>& item : ExtractCaptionDataFromSei(rbsp)) {
      result.push_back(std::move(item));
    }
  }
  return result;
}

}
