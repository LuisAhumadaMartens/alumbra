#ifndef PENUMBRA_DESKTOP_FFMPEG_FALLBACK_DEMUXER_H_
#define PENUMBRA_DESKTOP_FFMPEG_FALLBACK_DEMUXER_H_

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include "absl/status/statusor.h"
#include "penumbra/interface/fallback_demuxer.h"

namespace penumbra::desktop {

class FfmpegFallbackProvider final : public FallbackDemuxerProvider {
 public:
  [[nodiscard]] absl::StatusOr<FallbackProbe> ProbeFallbackDemuxer(
      std::span<const uint8_t> window,
      std::string_view name_hint) const override;

  absl::StatusOr<std::unique_ptr<FallbackDemuxer>> OpenFallbackDemuxer(
      FallbackDemuxerRequest request,
      std::shared_ptr<FallbackByteSource> source,
      std::shared_ptr<FallbackDemuxerCallbacks> callbacks) override;
};

}

#endif
