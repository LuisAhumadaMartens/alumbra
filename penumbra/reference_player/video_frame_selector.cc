#include "penumbra/reference_player/video_frame_selector.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/media_time.h"

namespace penumbra {

VideoFrameSelector::VideoFrameSelector(int32_t lateness_percent)
    : lateness_percent_(std::max(lateness_percent, 0)) {}

void VideoFrameSelector::SetLatenessPercent(int32_t lateness_percent) {
  lateness_percent_ = std::max(lateness_percent, 0);
}

void VideoFrameSelector::Reset() { dropped_frames_ = 0; }

VideoFrameSelection VideoFrameSelector::Select(
    std::span<const DecodedVideoFrame> frames, MediaTime media_time) {
  VideoFrameSelection result;
  if (!media_time.is_finite()) {
    return result;
  }
  const int64_t now_us = media_time.InMicroseconds();
  for (size_t index = 0; index < frames.size(); ++index) {
    const DecodedVideoFrame& frame = frames[index];
    if (!frame.presentation_time.is_finite()) {
      continue;
    }
    const int64_t start_us = frame.presentation_time.InMicroseconds();
    const int64_t duration_us =
        frame.duration.is_finite()
            ? std::max<int64_t>(frame.duration.InMicroseconds(), 0)
            : 0;
    if (start_us > now_us) {
      continue;
    }
    const int64_t lateness_us = now_us - start_us;
    const long double threshold =
        static_cast<long double>(duration_us) * lateness_percent_ / 100.0L;
    if (duration_us > 0 && static_cast<long double>(lateness_us) > threshold) {
      ++result.dropped_frames;
      continue;
    }
    if (!result.frame_index.has_value() ||
        frames[*result.frame_index].presentation_time <
            frame.presentation_time) {
      result.frame_index = index;
    }
  }
  dropped_frames_ += result.dropped_frames;
  return result;
}

uint64_t VideoFrameSelector::DroppedFrames() const { return dropped_frames_; }

}
