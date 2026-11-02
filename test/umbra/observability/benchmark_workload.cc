#include "benchmark_workload.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace umbra::observability {

std::array<std::uint8_t, kUnitBytes> MakeBuffer() {
  std::array<std::uint8_t, kUnitBytes> buffer{};
  std::uint32_t state = 0x12345678U;
  for (std::uint8_t& byte : buffer) {
    state = (state * 1664525U) + 1013904223U;
    byte = static_cast<std::uint8_t>(state >> 24);
  }
  return buffer;
}

}
