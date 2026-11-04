#include <cstddef>
#include <cstdint>
#include <string>

#include "benchmark/benchmark.h"
#include "umbra/manifest/hls_parser.h"

namespace umbra::manifest {
namespace {

std::string Playlist(std::size_t count) {
  std::string input = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n";
  for (std::size_t i = 0; i < count; ++i) {
    input.append("#EXTINF:4.0,\nsegment.ts\n");
  }
  input.append("#EXT-X-ENDLIST\n");
  return input;
}

void ParsePlaylist(benchmark::State& state) {
  const std::string input = Playlist(static_cast<std::size_t>(state.range(0)));
  for (auto _ : state) {
    static_cast<void>(_);
    const auto parsed = ParseHlsPlaylist(input);
    benchmark::DoNotOptimize(parsed.ok());
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<std::int64_t>(input.size()));
}

BENCHMARK(ParsePlaylist)->Arg(100)->Arg(1000)->Arg(10000);

}
}
