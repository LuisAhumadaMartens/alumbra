#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

#include "fuzz_target.h"
#include "umbra/text/cea_decoder.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  umbra::text::CeaDecoder decoder;
  const std::span<const std::uint8_t> input(data, size);
  const auto output = decoder.Push(input, 0.0);
  if (output.size() > (size / 3) + 8) {
    std::abort();
  }
  return 0;
}
