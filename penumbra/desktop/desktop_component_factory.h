#ifndef PENUMBRA_DESKTOP_DESKTOP_COMPONENT_FACTORY_H_
#define PENUMBRA_DESKTOP_DESKTOP_COMPONENT_FACTORY_H_

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/desktop/decoder_probe.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/decoder_types.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/stream_info.h"

namespace penumbra::desktop {

inline constexpr std::string_view kFfmpegDecoderPrefix = "ffmpeg.";

struct DesktopDecoderOptions {
  int32_t video_threads = 0;
};

class DesktopComponentFactory final : public ComponentFactory {
 public:
  explicit DesktopComponentFactory(
      DesktopDecoderOptions options = {},
      std::shared_ptr<const DecoderProbe> probe = nullptr);

  [[nodiscard]] absl::StatusOr<std::vector<DecoderCandidate>>
  GetDecoderCandidates(StreamType type) const override;
  absl::StatusOr<std::unique_ptr<Decoder>> CreateDecoder(
      const DecoderCandidate& candidate, StreamConfig config, SeekTicket ticket,
      std::shared_ptr<DecoderCallbacks> callbacks) override;
  absl::StatusOr<std::unique_ptr<VideoSink>> CreateVideoSink(
      OutputMode mode) override;
  absl::StatusOr<std::unique_ptr<AudioSink>> CreateAudioSink() override;

 private:
  DesktopDecoderOptions options_;
  std::shared_ptr<const DecoderProbe> probe_;
};

}

#endif
