#include "penumbra/desktop/av_sync_capture.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <ratio>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "penumbra/interface/media_time.h"

namespace penumbra::desktop {

AvSyncCapture::AvSyncCapture() : start_(std::chrono::steady_clock::now()) {}

double AvSyncCapture::CaptureTimeMilliseconds() const {
  return std::chrono::duration<double, std::milli>(
             std::chrono::steady_clock::now() - start_)
      .count();
}

void AvSyncCapture::RecordVideo(MediaTime media_time,
                                std::span<const uint8_t> rgba, int32_t width,
                                int32_t height, double present_ms) {
  if (media_time.is_unknown() || width <= 0 || height <= 0 || rgba.empty()) {
    return;
  }
  const size_t pixels =
      std::min(rgba.size() / 4,
               static_cast<size_t>(width) * static_cast<size_t>(height));
  if (pixels == 0) {
    return;
  }
  double sum = 0;
  for (size_t index = 0; index < pixels; ++index) {
    const size_t offset = index * 4;
    sum += (0.2126 * rgba[offset]) + (0.7152 * rgba[offset + 1]) +
           (0.0722 * rgba[offset + 2]);
  }
  const double luma = sum / (static_cast<double>(pixels) * 255.0);
  const std::scoped_lock lock(mutex_);
  video_.push_back({.media_time_us = media_time.InMicroseconds(),
                    .present_ms = present_ms,
                    .luma = luma});
}

void AvSyncCapture::RecordAudio(int64_t sample_position,
                                std::span<const float> samples,
                                int32_t channels, double capture_ms) {
  if (samples.empty() || channels <= 0) {
    return;
  }
  double sum = 0;
  for (const float sample : samples) {
    const double value = std::clamp(static_cast<double>(sample), -1.0, 1.0);
    sum += value * value;
  }
  const double level = std::sqrt(sum / static_cast<double>(samples.size()));
  const std::scoped_lock lock(mutex_);
  if (audio_.empty()) {
    first_audio_capture_ms_ = capture_ms;
  }
  audio_.push_back({.sample_position = sample_position, .level = level});
}

std::vector<AvSyncVideoObservation> AvSyncCapture::Video() const {
  const std::scoped_lock lock(mutex_);
  return video_;
}

std::vector<AvSyncAudioObservation> AvSyncCapture::Audio() const {
  const std::scoped_lock lock(mutex_);
  return audio_;
}

double AvSyncCapture::FirstAudioCaptureMs() const {
  const std::scoped_lock lock(mutex_);
  return first_audio_capture_ms_;
}

int32_t AvSyncCapture::SampleRate() const {
  const std::scoped_lock lock(mutex_);
  return sample_rate_;
}

void AvSyncCapture::SetSampleRate(int32_t sample_rate) {
  const std::scoped_lock lock(mutex_);
  sample_rate_ = sample_rate;
}

void AvSyncCapture::SetFailure(std::string failure) {
  const std::scoped_lock lock(mutex_);
  failure_ = std::move(failure);
}

std::string AvSyncCapture::Failure() const {
  const std::scoped_lock lock(mutex_);
  return failure_;
}

}
