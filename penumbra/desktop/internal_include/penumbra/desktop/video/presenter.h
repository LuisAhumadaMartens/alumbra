#ifndef PENUMBRA_DESKTOP_VIDEO_PRESENTER_H_
#define PENUMBRA_DESKTOP_VIDEO_PRESENTER_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/desktop/video/video_geometry.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/rational.h"

namespace penumbra::desktop {

enum class ScalingFilter : uint8_t {
  kLinear,
  kNearest,
};

enum class SurfaceKind : uint8_t {
  kTexturePlanes,
};

class GpuSurfaceHandle {
 public:
  virtual ~GpuSurfaceHandle() = default;
  [[nodiscard]] virtual SurfaceKind Kind() const = 0;
};

inline std::shared_ptr<const void> MakeOpaqueStorage(
    std::shared_ptr<const GpuSurfaceHandle> handle) {
  return handle;
}

struct RefreshRate {
  Rational hertz;

  [[nodiscard]] bool is_known() const { return hertz.is_positive(); }
  [[nodiscard]] TimeDelta Interval() const {
    if (!is_known()) {
      return TimeDelta::Unknown();
    }
    return TimeDelta::FromMicroseconds(
        ((hertz.denominator() * 1'000'000) + (hertz.numerator() / 2)) /
        hertz.numerator());
  }
};

enum class PresentTimingSupport : uint8_t {
  kNone,
  kPlatform,
};

struct PresenterCapabilities {
  std::string backend;
  bool offscreen = false;
  std::vector<SurfaceKind> imported_surface_kinds;
  PresentTimingSupport present_timing = PresentTimingSupport::kNone;
  std::string present_timing_fallback_reason;
  std::vector<PixelFormat> cpu_formats;
};

struct PresenterStats {
  uint64_t presented_frames = 0;
  uint64_t dropped_frames = 0;
  uint64_t repeated_refreshes = 0;
  uint64_t submitted_refreshes = 0;
  int64_t last_acquire_ticks_ns = 0;
  int64_t last_submit_ticks_ns = 0;
  MediaTime last_presented_media_time = MediaTime::Unknown();
};

struct PresentRecord {
  uint64_t refresh_sequence = 0;
  bool new_frame = false;
  MediaTime media_time = MediaTime::Unknown();
  int64_t acquire_ticks_ns = 0;
  int64_t submit_ticks_ns = 0;
  PixelSize target_pixels;
  PixelRect video_rect;
};

class PresenterObserver {
 public:
  virtual ~PresenterObserver() = default;
  virtual void OnRefreshRateChanged(const RefreshRate& rate) = 0;
  virtual void OnPresented(const PresentRecord& record) = 0;
};

struct PresentRequest {
  const DecodedVideoFrame* frame = nullptr;
  Rational sample_aspect_ratio = Rational::Of(1, 1);
  std::optional<PixelRect> bounds;
};

struct RgbaImage {
  PixelSize size;
  std::vector<uint8_t> pixels;
};

class Presenter {
 public:
  virtual ~Presenter() = default;
  virtual absl::Status Present(const PresentRequest& request) = 0;
  virtual absl::Status SetScalingFilter(ScalingFilter filter) = 0;
  virtual void SetObserver(std::shared_ptr<PresenterObserver> observer) = 0;
  [[nodiscard]] virtual RefreshRate CurrentRefreshRate() const = 0;
  [[nodiscard]] virtual PresenterStats Stats() const = 0;
  [[nodiscard]] virtual PresenterCapabilities Capabilities() const = 0;
};

}

#endif
