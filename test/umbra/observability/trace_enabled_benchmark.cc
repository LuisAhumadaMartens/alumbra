#include <cstddef>
#include <cstdint>
#include <span>

#include "benchmark/benchmark.h"
#include "benchmark_workload.h"
#include "umbra/observability/category.h"
#include "umbra/observability/log.h"
#include "umbra/observability/trace.h"

namespace umbra::observability {
namespace {

void RunUnits(benchmark::State& state, std::uint32_t trace_mask) {
  SetLogLevel(Level::kInfo);
  SetSink(nullptr);
  SetTraceCategoryMask(trace_mask);
  ClearTrace();
  const auto buffer = MakeBuffer();
  const std::span<const std::uint8_t> bytes(
      buffer.data(), static_cast<std::size_t>(state.range(0)));
  for (auto _ : state) {
    static_cast<void>(_);
    std::uint64_t result = ProcessUnitInstrumented(bytes);
    benchmark::DoNotOptimize(result);
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
  SetTraceCategoryMask(0);
}

void TraceCaptureDisabledReference(benchmark::State& state) {
  RunUnits(state, 0);
}

void TraceCaptureEnabled(benchmark::State& state) {
  RunUnits(state, CategoryBit(Category::kDemux));
}

BENCHMARK(TraceCaptureDisabledReference)->Arg(kUnitBytes)->Arg(kFineUnitBytes);
BENCHMARK(TraceCaptureEnabled)->Arg(kUnitBytes)->Arg(kFineUnitBytes);

}
}

BENCHMARK_MAIN();
