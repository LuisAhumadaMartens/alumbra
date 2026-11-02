#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "fuzz_target.h"

namespace {

volatile std::uint8_t* leaked_sink = nullptr;

}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::span<const std::uint8_t> input(data, size);
  if (!input.empty()) {
    auto bytes = std::make_unique<std::uint8_t[]>(input.size());
    bytes[0] = input[0];
    leaked_sink = bytes.release();
    leaked_sink = nullptr;
  }
  return 0;
}
