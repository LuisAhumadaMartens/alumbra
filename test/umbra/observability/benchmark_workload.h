#ifndef TEST_UMBRA_OBSERVABILITY_BENCHMARK_WORKLOAD_H_
#define TEST_UMBRA_OBSERVABILITY_BENCHMARK_WORKLOAD_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "umbra/observability/log.h"
#include "umbra/observability/trace.h"

namespace umbra::observability {

inline constexpr std::size_t kUnitBytes = 16384;
inline constexpr std::size_t kFineUnitBytes = 256;

[[nodiscard]] std::array<std::uint8_t, kUnitBytes> MakeBuffer();

[[gnu::noinline]] inline std::uint64_t ParseUnit(
    std::span<const std::uint8_t> bytes) {
  std::uint64_t state = 0xcbf29ce484222325ULL;
  for (const std::uint8_t byte : bytes) {
    state ^= byte;
    state *= 0x100000001b3ULL;
    state ^= state >> 29;
  }
  return state;
}

[[gnu::noinline]] inline std::uint64_t ProcessUnitBaseline(
    std::span<const std::uint8_t> bytes) {
  return ParseUnit(bytes);
}

[[gnu::noinline]] inline std::uint64_t ProcessUnitInstrumented(
    std::span<const std::uint8_t> bytes) {
  ALUMBRA_TRACE(kDemux, "process_unit");
  const std::uint64_t result = ParseUnit(bytes);
  ALUMBRA_TRACE_COUNTER(kDemux, "unit_bytes", bytes.size());
  ALUMBRA_LOG_EVERY_N(kDemux, kVerbose, 4096).Field("unit_bytes", bytes.size())
      << "unit processed";
  return result;
}

}

#endif
