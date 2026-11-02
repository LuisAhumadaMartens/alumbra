#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "fuzz_target.h"

namespace {

constexpr std::size_t kMebibyte = std::size_t{1} << 20U;
constexpr std::size_t kAllocation =
    (std::size_t{ALUMBRA_FUZZ_RSS_LIMIT_MB} + 1) * kMebibyte;

}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::span<const std::uint8_t> input(data, size);
  if (!input.empty()) {
    std::vector<std::uint8_t> bytes(kAllocation);
    bytes.back() = input[0];
    return bytes.back();
  }
  return 0;
}
