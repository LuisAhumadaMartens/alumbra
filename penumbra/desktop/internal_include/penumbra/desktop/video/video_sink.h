#ifndef PENUMBRA_DESKTOP_VIDEO_VIDEO_SINK_H_
#define PENUMBRA_DESKTOP_VIDEO_VIDEO_SINK_H_

#include <cstdint>
#include <memory>
#include <optional>

#include "absl/status/status.h"
#include "penumbra/desktop/video/presenter.h"
#include "penumbra/desktop/video/video_geometry.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/stream_info.h"

namespace penumbra::desktop {

struct VideoPlaybackQuality {
  uint64_t presented_frames = 0;
  uint64_t dropped_frames = 0;
};

class DesktopVideoSink final : public VideoSink {
 public:
  explicit DesktopVideoSink(std::shared_ptr<Presenter> presenter);

  absl::Status Start(std::shared_ptr<VideoFrameSource> frames) override;
  absl::Status SetBounds(OutputBounds bounds) override;
  absl::Status Stop() override;

  void SetVideoInfo(const VideoInfo& info);
  absl::Status RunFrame(MediaTime media_time);
  [[nodiscard]] VideoPlaybackQuality PlaybackQuality() const;

 private:
  std::shared_ptr<Presenter> presenter_;
  std::shared_ptr<VideoFrameSource> frames_;
  std::optional<PixelRect> bounds_;
  Rational sample_aspect_ratio_ = Rational::Of(1, 1);
};

}

#endif
