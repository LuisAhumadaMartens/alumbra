#ifndef PENUMBRA_REFERENCE_PLAYER_VIDEO_FRAME_SELECTOR_H_
#define PENUMBRA_REFERENCE_PLAYER_VIDEO_FRAME_SELECTOR_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/media_time.h"

namespace penumbra {

struct VideoFrameSelection {
  std::optional<size_t> frame_index;
  uint64_t dropped_frames = 0;
};

class VideoFrameSelector {
 public:
  explicit VideoFrameSelector(int32_t lateness_percent = 100);

  void SetLatenessPercent(int32_t lateness_percent);
  void Reset();
  [[nodiscard]] VideoFrameSelection Select(
      std::span<const DecodedVideoFrame> frames, MediaTime media_time);
  [[nodiscard]] uint64_t DroppedFrames() const;

 private:
  int32_t lateness_percent_;
  uint64_t dropped_frames_ = 0;
};

}

#endif
