#include <cstdint>
#include <memory>

#include "benchmark/benchmark.h"
#include "penumbra/desktop/capability_provider.h"
#include "penumbra/desktop/software_decoder_probe.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/stream_info.h"

namespace penumbra::desktop {
namespace {

CapabilityRequest Request(int32_t width) {
  CapabilityRequest request;
  request.mime_type = "video/mp4; codecs=\"avc1.64001f\"";
  request.stream.codec = CodecId::kH264;
  request.stream.codec_string = "avc1.64001f";
  VideoInfo video;
  video.coded_width = width;
  video.coded_height = 1080;
  video.frame_rate = Rational::Of(60, 1);
  request.stream.format = video;
  return request;
}

void QueryCached(benchmark::State& state) {
  const DesktopCapabilityProvider provider(
      DecodeLimits{}, std::make_shared<SoftwareDecoderProbe>());
  const CapabilityRequest request = Request(1920);
  benchmark::DoNotOptimize(provider.QueryDecoding(request));
  for (auto _ : state) {
    static_cast<void>(_);
    benchmark::DoNotOptimize(provider.QueryDecoding(request));
  }
}
BENCHMARK(QueryCached);

void QueryUncached(benchmark::State& state) {
  const DesktopCapabilityProvider provider(
      DecodeLimits{}, std::make_shared<SoftwareDecoderProbe>());
  int32_t width = 1;
  for (auto _ : state) {
    static_cast<void>(_);
    benchmark::DoNotOptimize(provider.QueryDecoding(Request(width)));
    width = (width % 100000) + 1;
  }
}
BENCHMARK(QueryUncached);

}
}

BENCHMARK_MAIN();
