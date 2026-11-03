#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "fuzz_target.h"
#include "umbra/demux/ts_packet.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::span<const std::uint8_t> input(data, size);
  umbra::demux::ts::SectionAssembler pat_assembler;
  umbra::demux::ts::SectionAssembler pmt_assembler;
  for (std::size_t offset = 0; offset < input.size();
       offset += umbra::demux::ts::kPacketSize) {
    const std::size_t count =
        std::min(umbra::demux::ts::kPacketSize, input.size() - offset);
    const std::span<const std::uint8_t> packet = input.subspan(offset, count);
    const auto header = umbra::demux::ts::ParsePacket(packet);
    if (!header.has_value() || !header->has_payload) {
      continue;
    }
    std::vector<umbra::demux::ts::Section> sections;
    if (header->pid == umbra::demux::ts::kPatPid) {
      pat_assembler.Feed(header->payload, header->payload_unit_start, sections);
    } else {
      pmt_assembler.Feed(header->payload, header->payload_unit_start, sections);
    }
    for (const umbra::demux::ts::Section& section : sections) {
      static_cast<void>(umbra::demux::ts::ParsePat(section.bytes));
      static_cast<void>(umbra::demux::ts::ParsePmt(section.bytes));
    }
  }
  std::vector<umbra::demux::ts::Section> sections;
  pat_assembler.Feed(input, true, sections);
  pmt_assembler.Feed(input, true, sections);
  for (const umbra::demux::ts::Section& section : sections) {
    static_cast<void>(umbra::demux::ts::ParsePat(section.bytes));
    static_cast<void>(umbra::demux::ts::ParsePmt(section.bytes));
  }
  static_cast<void>(umbra::demux::ts::ParsePat(input));
  static_cast<void>(umbra::demux::ts::ParsePmt(input));
  static_cast<void>(umbra::demux::ts::ParsePesHeader(input));
  return 0;
}
