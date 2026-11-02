#include <cstddef>
#include <cstdint>
#include <span>

#include "fuzz_target.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::span<const std::uint8_t> input(data, size);
  if (input.size() >= 4 && input[0] == 'F' && input[1] == 'U' &&
      input[2] == 'Z' && input[3] == 'Z') {
    const volatile std::size_t end = input.size();
    return data[end];
  }
  return 0;
}
