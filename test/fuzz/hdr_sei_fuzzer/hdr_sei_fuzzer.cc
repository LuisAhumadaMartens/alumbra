#include <cstddef>
#include <cstdint>
#include <span>

#include "fuzz_target.h"
#include "umbra/demux/caption_extractor.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  if (size == 0) {
    return 0;
  }
  const std::span<const std::uint8_t> input(data, size);
  const std::uint8_t selector = input.front();
  const auto codec = (selector & 1U) == 0 ? umbra::demux::CaptionCodec::kH264
                                          : umbra::demux::CaptionCodec::kHevc;
  const std::size_t length_size = ((selector >> 1U) % 4U) + 1U;
  const std::span<const std::uint8_t> sample = input.subspan(1);
  static_cast<void>(
      umbra::demux::ExtractHdrDynamicMetadata(codec, length_size, sample));
  return 0;
}
