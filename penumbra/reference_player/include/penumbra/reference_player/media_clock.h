#ifndef PENUMBRA_REFERENCE_PLAYER_MEDIA_CLOCK_H_
#define PENUMBRA_REFERENCE_PLAYER_MEDIA_CLOCK_H_

#include <atomic>
#include <cstdint>

#include "penumbra/interface/media_time.h"

namespace penumbra {

struct AudioClockUpdate {
  MediaTime media_time = MediaTime::Unknown();
  TimeDelta output_latency = TimeDelta::Unknown();
  TimeDelta audio_offset = TimeDelta::Zero();
  int64_t observed_at_us = 0;
};

class MediaClock {
 public:
  MediaClock();

  void Reset(MediaTime position, int64_t monotonic_us);
  void SetRate(double rate, int64_t monotonic_us);
  void SetResampleRatio(double ratio, int64_t monotonic_us);
  void SetPaused(bool paused, int64_t monotonic_us);
  void SetAudioActive(bool active, int64_t monotonic_us);
  void UpdateAudio(const AudioClockUpdate& update, int64_t monotonic_us);
  void SlewAudioLatency(TimeDelta latency, TimeDelta audio_offset,
                        int64_t monotonic_us, TimeDelta duration);
  [[nodiscard]] MediaTime Now(int64_t monotonic_us) const;
  [[nodiscard]] double Rate() const;

 private:
  struct State {
    int64_t anchor_monotonic_us = 0;
    int64_t anchor_media_us = 0;
    int64_t last_media_us = 0;
    int64_t audio_media_us = 0;
    int64_t audio_observed_us = 0;
    int64_t latency_us = 0;
    int64_t offset_us = 0;
    int64_t slew_start_us = 0;
    int64_t slew_end_us = 0;
    int64_t slew_from_us = 0;
    int64_t slew_to_us = 0;
    double playback_rate = 0.0;
    double resample_ratio = 1.0;
    bool audio_active = false;
    bool paused = true;
  };

  [[nodiscard]] State ReadState() const;
  void WriteState(const State& state);
  void Reanchor(State& state, int64_t monotonic_us, int64_t media_us) const;

  std::atomic<uint64_t> sequence_{0};
  std::atomic<int64_t> anchor_monotonic_us_{0};
  std::atomic<int64_t> anchor_media_us_{0};
  mutable std::atomic<int64_t> last_media_us_{0};
  std::atomic<int64_t> audio_media_us_{0};
  std::atomic<int64_t> audio_observed_us_{0};
  std::atomic<int64_t> latency_us_{0};
  std::atomic<int64_t> offset_us_{0};
  std::atomic<int64_t> slew_start_us_{0};
  std::atomic<int64_t> slew_end_us_{0};
  std::atomic<int64_t> slew_from_us_{0};
  std::atomic<int64_t> slew_to_us_{0};
  std::atomic<double> playback_rate_{0.0};
  std::atomic<double> resample_ratio_{1.0};
  std::atomic<bool> audio_active_{false};
  std::atomic<bool> paused_{true};
};

}

#endif
