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

void DisableObservability() {
  SetTraceCategoryMask(0);
  SetLogLevel(Level::kInfo);
  SetLogCategoryMask(kAllCategories);
  SetSink(nullptr);
}

template <bool kInstrumented>
void RunUnits(benchmark::State& state) {
  DisableObservability();
  const auto buffer = MakeBuffer();
  const std::span<const std::uint8_t> bytes(
      buffer.data(), static_cast<std::size_t>(state.range(0)));
  for (auto _ : state) {
    static_cast<void>(_);
    std::uint64_t result = 0;
    if constexpr (kInstrumented) {
      result = ProcessUnitInstrumented(bytes);
    } else {
      result = ProcessUnitBaseline(bytes);
    }
    benchmark::DoNotOptimize(result);
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}

void NoObservabilityBaseline(benchmark::State& state) {
  RunUnits<false>(state);
}

void RuntimeDisabledObservability(benchmark::State& state) {
  RunUnits<true>(state);
}

BENCHMARK(NoObservabilityBaseline)->Arg(kUnitBytes)->Arg(kFineUnitBytes);
BENCHMARK(RuntimeDisabledObservability)->Arg(kUnitBytes)->Arg(kFineUnitBytes);

}
}

BENCHMARK_MAIN();
