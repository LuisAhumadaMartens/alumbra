#ifndef PENUMBRA_DESKTOP_FFMPEG_FRAME_H_
#define PENUMBRA_DESKTOP_FFMPEG_FRAME_H_

#include <memory>

#include "absl/status/statusor.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/rational.h"

struct AVFrame;

namespace penumbra::desktop {

struct AvFrameDeleter {
  void operator()(AVFrame* frame) const;
};

using AvFramePtr = std::unique_ptr<AVFrame, AvFrameDeleter>;

[[nodiscard]] AvFramePtr AllocateAvFrame();

[[nodiscard]] bool IsCorruptFrame(const AVFrame& frame);

[[nodiscard]] absl::StatusOr<DecodedVideoFrame> WrapVideoFrame(
    AvFramePtr frame, Rational time_base, Rational frame_rate);

[[nodiscard]] absl::StatusOr<DecodedAudioBuffer> WrapAudioFrame(
    AvFramePtr frame, Rational time_base, MediaTime fallback_time);

}

#endif
