#include "penumbra/desktop/video/video_sink.h"

#include <memory>
#include <optional>
#include <utility>

#include "absl/status/status.h"
#include "penumbra/desktop/video/presenter.h"
#include "penumbra/desktop/video/video_geometry.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/stream_info.h"

namespace penumbra::desktop {

DesktopVideoSink::DesktopVideoSink(std::shared_ptr<Presenter> presenter)
    : presenter_(std::move(presenter)) {}

absl::Status DesktopVideoSink::Start(std::shared_ptr<VideoFrameSource> frames) {
  if (frames == nullptr) {
    return absl::InvalidArgumentError("A video sink needs a frame source");
  }
  frames_ = std::move(frames);
  return absl::OkStatus();
}

absl::Status DesktopVideoSink::SetBounds(OutputBounds bounds) {
  if (bounds.width <= 0 || bounds.height <= 0) {
    bounds_.reset();
    return absl::OkStatus();
  }
  bounds_ = PixelRect{.x = bounds.x,
                      .y = bounds.y,
                      .width = bounds.width,
                      .height = bounds.height};
  return absl::OkStatus();
}

absl::Status DesktopVideoSink::Stop() {
  frames_.reset();
  return absl::OkStatus();
}

void DesktopVideoSink::SetVideoInfo(const VideoInfo& info) {
  sample_aspect_ratio_ = info.sample_aspect_ratio.is_positive()
                             ? info.sample_aspect_ratio
                             : Rational::Of(1, 1);
}

absl::Status DesktopVideoSink::RunFrame(MediaTime media_time) {
  if (frames_ == nullptr) {
    return absl::FailedPreconditionError("The video sink is not started");
  }
  std::optional<DecodedVideoFrame> due = frames_->TakeDueFrame(media_time);
  PresentRequest request;
  request.frame = due.has_value() ? &*due : nullptr;
  request.sample_aspect_ratio = sample_aspect_ratio_;
  request.bounds = bounds_;
  return presenter_->Present(request);
}

VideoPlaybackQuality DesktopVideoSink::PlaybackQuality() const {
  const PresenterStats stats = presenter_->Stats();
  return VideoPlaybackQuality{.presented_frames = stats.presented_frames,
                              .dropped_frames = stats.dropped_frames};
}

}
