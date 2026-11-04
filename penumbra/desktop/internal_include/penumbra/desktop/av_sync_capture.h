#ifndef PENUMBRA_DESKTOP_AV_SYNC_CAPTURE_H_
#define PENUMBRA_DESKTOP_AV_SYNC_CAPTURE_H_

#include <chrono>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <vector>

#include "penumbra/interface/media_time.h"

namespace penumbra::desktop {

struct AvSyncVideoObservation {
  int64_t media_time_us = 0;
  double present_ms = 0;
  double luma = 0;
};

struct AvSyncAudioObservation {
  int64_t sample_position = 0;
  double level = 0;
};

class AvSyncCapture final {
 public:
  AvSyncCapture();

  void RecordVideo(MediaTime media_time, std::span<const uint8_t> rgba,
                   int32_t width, int32_t height, double present_ms);
  void RecordAudio(int64_t sample_position, std::span<const float> samples,
                   int32_t channels, double capture_ms);
  [[nodiscard]] double CaptureTimeMilliseconds() const;
  [[nodiscard]] std::vector<AvSyncVideoObservation> Video() const;
  [[nodiscard]] std::vector<AvSyncAudioObservation> Audio() const;
  [[nodiscard]] double FirstAudioCaptureMs() const;
  [[nodiscard]] int32_t SampleRate() const;
  void SetSampleRate(int32_t sample_rate);
  void SetFailure(std::string failure);
  [[nodiscard]] std::string Failure() const;

 private:
  std::chrono::steady_clock::time_point start_;
  mutable std::mutex mutex_;
  std::vector<AvSyncVideoObservation> video_;
  std::vector<AvSyncAudioObservation> audio_;
  double first_audio_capture_ms_ = 0;
  int32_t sample_rate_ = 0;
  std::string failure_;
};

}

#endif
