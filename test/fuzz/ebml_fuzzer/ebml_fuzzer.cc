#include <cstddef>
#include <cstdint>

#include "fuzz_target.h"
#include "umbra/demux/ebml.h"
#include "umbra/demux/mkv_elements.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const umbra::demux::EbmlBytes input(data, size);
  static_cast<void>(umbra::demux::ParseEbmlHeader(input));
  static_cast<void>(umbra::demux::ParseVint(input));
  umbra::demux::EbmlReader reader(input);
  while (!reader.AtEnd()) {
    const auto element = reader.Next();
    if (!element.ok()) {
      break;
    }
    static_cast<void>(umbra::demux::ReadEbmlUnsigned(element->payload));
    static_cast<void>(umbra::demux::ReadEbmlSigned(element->payload));
    static_cast<void>(umbra::demux::ReadEbmlFloat(element->payload));
    static_cast<void>(umbra::demux::ReadEbmlString(element->payload));
    if (element->id == umbra::demux::kIdEbml) {
      static_cast<void>(umbra::demux::ParseEbmlHeaderElement(element->payload));
    } else if (element->id == umbra::demux::kIdSeekHead) {
      static_cast<void>(umbra::demux::ParseSeekHead(element->payload));
    } else if (element->id == umbra::demux::kIdInfo) {
      static_cast<void>(umbra::demux::ParseInfo(element->payload));
    } else if (element->id == umbra::demux::kIdTracks) {
      static_cast<void>(umbra::demux::ParseTracks(element->payload));
    } else if (element->id == umbra::demux::kIdCues) {
      static_cast<void>(umbra::demux::ParseCues(element->payload));
    } else if (element->id == umbra::demux::kIdBlockGroup) {
      static_cast<void>(umbra::demux::ParseBlockGroup(element->payload));
    }
  }
  return 0;
}
