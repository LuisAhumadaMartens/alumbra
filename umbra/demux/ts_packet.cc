#include "umbra/demux/ts_packet.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace umbra::demux::ts {
namespace {

constexpr size_t kPacketHeaderSize = 4;
constexpr size_t kSectionHeaderSize = 3;
constexpr size_t kSectionTrailerSize = 4;
constexpr size_t kLongSectionHeaderSize = 8;
constexpr uint8_t kStuffingByte = 0xFF;
constexpr uint8_t kPatTableId = 0x00;
constexpr uint8_t kPmtTableId = 0x02;
constexpr uint8_t kDescriptorRegistration = 0x05;
constexpr uint8_t kDescriptorLanguage = 0x0A;
constexpr uint8_t kDescriptorAc3 = 0x6A;
constexpr uint8_t kDescriptorEac3 = 0x7A;
constexpr size_t kMaxPmtStreams = 64;

constexpr std::array<uint32_t, 256> MakeCrcTable() {
  std::array<uint32_t, 256> table{};
  for (uint32_t index = 0; index < 256; ++index) {
    uint32_t value = index << 24U;
    for (int bit = 0; bit < 8; ++bit) {
      value = (value & 0x80000000U) != 0 ? (value << 1U) ^ 0x04C11DB7U
                                         : value << 1U;
    }
    table.at(index) = value;
  }
  return table;
}

constexpr std::array<uint32_t, 256> kCrcTable = MakeCrcTable();

uint16_t Be16(std::span<const uint8_t> data, size_t offset) {
  return static_cast<uint16_t>((data[offset] << 8U) | data[offset + 1]);
}

std::optional<uint64_t> ParseTimestamp(std::span<const uint8_t> data,
                                       size_t offset, uint8_t marker) {
  if (offset + 5 > data.size()) {
    return std::nullopt;
  }
  const uint8_t first = data[offset];
  if ((first >> 4U) != marker || (first & 1U) == 0 ||
      (data[offset + 2] & 1U) == 0 || (data[offset + 4] & 1U) == 0) {
    return std::nullopt;
  }
  const uint64_t high = (first >> 1U) & 7U;
  const uint64_t middle = (static_cast<uint64_t>(data[offset + 1]) << 7U) |
                          (data[offset + 2] >> 1U);
  const uint64_t low = (static_cast<uint64_t>(data[offset + 3]) << 7U) |
                       (data[offset + 4] >> 1U);
  return (high << 30U) | (middle << 15U) | low;
}

EsDescriptors ParseDescriptors(std::span<const uint8_t> data) {
  EsDescriptors result;
  size_t position = 0;
  while (position + 2 <= data.size()) {
    const uint8_t tag = data[position];
    const size_t length = data[position + 1];
    position += 2;
    if (position + length > data.size()) {
      break;
    }
    const std::span<const uint8_t> body = data.subspan(position, length);
    position += length;
    if (tag == kDescriptorAc3) {
      result.ac3 = true;
    } else if (tag == kDescriptorEac3) {
      result.eac3 = true;
    } else if (tag == kDescriptorRegistration && body.size() >= 4) {
      const std::string format(body.begin(), body.begin() + 4);
      result.ac3 = result.ac3 || format == "AC-3";
      result.eac3 = result.eac3 || format == "EAC3";
      result.id3 = result.id3 || format == "ID3 ";
    } else if (tag == kDescriptorLanguage && body.size() >= 4 &&
               result.language.empty()) {
      result.language.assign(body.begin(), body.begin() + 3);
    }
  }
  return result;
}

bool SectionHeaderValid(std::span<const uint8_t> section, uint8_t table_id) {
  if (section.size() < kLongSectionHeaderSize + kSectionTrailerSize ||
      section[0] != table_id || (section[1] & 0x80U) == 0) {
    return false;
  }
  const size_t length = ((section[1] & 0x0FU) << 8U) | section[2];
  return section.size() == length + kSectionHeaderSize &&
         (section[5] & 1U) != 0;
}

}

std::optional<PacketHeader> ParsePacket(std::span<const uint8_t> packet) {
  if (packet.size() < kPacketSize || packet[0] != kSyncByte) {
    return std::nullopt;
  }
  PacketHeader header;
  header.transport_error = (packet[1] & 0x80U) != 0;
  header.payload_unit_start = (packet[1] & 0x40U) != 0;
  header.pid = static_cast<uint16_t>(((packet[1] & 0x1FU) << 8U) | packet[2]);
  header.scrambling = static_cast<uint8_t>(packet[3] >> 6U);
  const uint8_t control = (packet[3] >> 4U) & 3U;
  header.continuity = packet[3] & 0x0FU;
  if (control == 0) {
    return std::nullopt;
  }
  size_t position = kPacketHeaderSize;
  if ((control & 2U) != 0) {
    const size_t length = packet[position];
    ++position;
    if (position + length > kPacketSize) {
      return std::nullopt;
    }
    if (length > 0) {
      const uint8_t flags = packet[position];
      header.adaptation.discontinuity = (flags & 0x80U) != 0;
      header.adaptation.random_access = (flags & 0x40U) != 0;
      if ((flags & 0x10U) != 0) {
        if (length < 7) {
          return std::nullopt;
        }
        uint64_t base = 0;
        for (size_t index = 1; index <= 4; ++index) {
          base = (base << 8U) | packet[position + index];
        }
        base = (base << 1U) | (packet[position + 5] >> 7U);
        const uint64_t extension =
            (static_cast<uint64_t>(packet[position + 5] & 1U) << 8U) |
            packet[position + 6];
        header.adaptation.pcr_27mhz = (base * 300) + extension;
      }
    }
    position += length;
  }
  header.has_payload = (control & 1U) != 0;
  if (header.has_payload) {
    header.payload = packet.subspan(position, kPacketSize - position);
  }
  return header;
}

uint32_t Crc32Mpeg(std::span<const uint8_t> data) {
  uint32_t crc = 0xFFFFFFFFU;
  for (const uint8_t byte : data) {
    crc = (crc << 8U) ^ kCrcTable.at(((crc >> 24U) ^ byte) & 0xFFU);
  }
  return crc;
}

void SectionAssembler::Reset() {
  buffer_.clear();
  expected_ = 0;
  active_ = false;
}

void SectionAssembler::Feed(std::span<const uint8_t> payload,
                            bool payload_unit_start,
                            std::vector<Section>& complete) {
  if (!payload_unit_start) {
    if (active_) {
      Consume(payload, complete);
    }
    return;
  }
  if (payload.empty()) {
    return;
  }
  const size_t pointer = payload[0];
  if (1 + pointer > payload.size()) {
    Reset();
    return;
  }
  if (active_) {
    Consume(payload.subspan(1, pointer), complete);
    Reset();
  }
  Consume(payload.subspan(1 + pointer), complete);
}

void SectionAssembler::Consume(std::span<const uint8_t> data,
                               std::vector<Section>& complete) {
  size_t used = 0;
  while (used < data.size()) {
    if (!active_) {
      if (data[used] == kStuffingByte) {
        return;
      }
      active_ = true;
      buffer_.clear();
      expected_ = 0;
    }
    const size_t need = expected_ == 0 ? kSectionHeaderSize - buffer_.size()
                                       : expected_ - buffer_.size();
    const size_t take = std::min(need, data.size() - used);
    buffer_.insert(buffer_.end(), data.begin() + static_cast<ptrdiff_t>(used),
                   data.begin() + static_cast<ptrdiff_t>(used + take));
    used += take;
    if (expected_ == 0 && buffer_.size() == kSectionHeaderSize) {
      expected_ =
          kSectionHeaderSize +
          ((static_cast<size_t>(buffer_[1] & 0x0FU) << 8U) | buffer_[2]);
      if (expected_ > kMaxSectionBytes) {
        ++oversized_;
        Reset();
        return;
      }
    }
    if (expected_ != 0 && buffer_.size() == expected_) {
      Section section;
      section.crc_ok = (buffer_[1] & 0x80U) == 0 || Crc32Mpeg(buffer_) == 0;
      section.bytes = std::move(buffer_);
      complete.push_back(std::move(section));
      Reset();
    }
  }
}

std::optional<Pat> ParsePat(std::span<const uint8_t> section) {
  if (!SectionHeaderValid(section, kPatTableId)) {
    return std::nullopt;
  }
  const size_t body = section.size() - kSectionTrailerSize;
  if ((body - kLongSectionHeaderSize) % 4 != 0) {
    return std::nullopt;
  }
  Pat pat;
  pat.version = (section[5] >> 1U) & 0x1FU;
  for (size_t position = kLongSectionHeaderSize; position < body;
       position += 4) {
    PatEntry entry;
    entry.program_number = Be16(section, position);
    entry.pid = static_cast<uint16_t>(Be16(section, position + 2) & 0x1FFFU);
    pat.programs.push_back(entry);
  }
  return pat;
}

std::optional<Pmt> ParsePmt(std::span<const uint8_t> section) {
  if (!SectionHeaderValid(section, kPmtTableId) ||
      section.size() < kLongSectionHeaderSize + 4 + kSectionTrailerSize) {
    return std::nullopt;
  }
  const size_t body = section.size() - kSectionTrailerSize;
  Pmt pmt;
  pmt.program_number = Be16(section, 3);
  pmt.version = (section[5] >> 1U) & 0x1FU;
  pmt.pcr_pid = static_cast<uint16_t>(Be16(section, 8) & 0x1FFFU);
  const size_t program_info = Be16(section, 10) & 0x0FFFU;
  size_t position = kLongSectionHeaderSize + 4 + program_info;
  while (position + 5 <= body) {
    PmtStream stream;
    stream.stream_type = section[position];
    stream.pid = static_cast<uint16_t>(Be16(section, position + 1) & 0x1FFFU);
    const size_t info = Be16(section, position + 3) & 0x0FFFU;
    position += 5;
    if (position + info > body) {
      return std::nullopt;
    }
    stream.descriptors = ParseDescriptors(section.subspan(position, info));
    position += info;
    if (pmt.streams.size() >= kMaxPmtStreams) {
      break;
    }
    pmt.streams.push_back(std::move(stream));
  }
  return pmt;
}

bool StreamIdHasPesHeader(uint8_t stream_id) {
  switch (stream_id) {
    case 0xBC:
    case 0xBE:
    case 0xBF:
    case 0xF0:
    case 0xF1:
    case 0xF2:
    case 0xF8:
    case 0xFF:
      return false;
    default:
      return true;
  }
}

std::optional<PesHeader> ParsePesHeader(std::span<const uint8_t> pes) {
  if (pes.size() < 6 || pes[0] != 0 || pes[1] != 0 || pes[2] != 1) {
    return std::nullopt;
  }
  PesHeader header;
  header.stream_id = pes[3];
  header.packet_length = Be16(pes, 4);
  if (!StreamIdHasPesHeader(header.stream_id)) {
    header.payload_offset = 6;
    return header;
  }
  if (pes.size() < 9 || (pes[6] >> 6U) != 2) {
    return std::nullopt;
  }
  const uint8_t flags = pes[7] >> 6U;
  const size_t header_length = pes[8];
  header.payload_offset = 9 + header_length;
  if (header.payload_offset > pes.size() || flags == 1) {
    return std::nullopt;
  }
  if (flags >= 2) {
    header.pts = ParseTimestamp(pes, 9, flags == 2 ? 2 : 3);
    if (!header.pts.has_value()) {
      return std::nullopt;
    }
  }
  if (flags == 3) {
    header.dts = ParseTimestamp(pes, 14, 1);
    if (!header.dts.has_value()) {
      return std::nullopt;
    }
  }
  return header;
}

int64_t TimestampUnwrapper::Unwrap(uint64_t raw) {
  const auto wrap = static_cast<int64_t>(kTimestampWrap);
  auto value = static_cast<int64_t>(raw & (kTimestampWrap - 1));
  if (reference_.has_value()) {
    const int64_t shifted = *reference_ - value + (wrap / 2);
    int64_t cycles = shifted / wrap;
    if (shifted < 0 && shifted % wrap != 0) {
      --cycles;
    }
    value += cycles * wrap;
  }
  reference_ = value;
  return value;
}

}
