#include <cstddef>
#include <cstdint>
#include <span>

#include "fuzz_target.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::span<const std::uint8_t> input(data, size);
  volatile bool spinning = !input.empty();
  while (spinning) {
  }
  return 0;
}
