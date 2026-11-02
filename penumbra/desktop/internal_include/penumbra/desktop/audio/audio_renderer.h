#ifndef PENUMBRA_DESKTOP_AUDIO_AUDIO_RENDERER_H_
#define PENUMBRA_DESKTOP_AUDIO_AUDIO_RENDERER_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "absl/status/statusor.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/stream_info.h"

namespace penumbra::desktop {

class MonotonicClock {
 public:
  virtual ~MonotonicClock() = default;
  [[nodiscard]] virtual int64_t NowNanoseconds() const noexcept = 0;
};

const MonotonicClock& SteadyClock();

struct AudioRendererConfig {
  int32_t sample_rate = 0;
  int32_t channel_count = 0;
  SampleFormat sample_format = SampleFormat::kF32;
  int32_t ramp_milliseconds = 5;
  size_t max_chunk_frames = 4096;
};

class AudioRenderer {
 public:
  static absl::StatusOr<std::unique_ptr<AudioRenderer>> Create(
      const AudioRendererConfig& config, std::shared_ptr<AudioSource> source,
      const MonotonicClock* clock);

  ~AudioRenderer() = default;
  AudioRenderer(const AudioRenderer&) = delete;
  AudioRenderer& operator=(const AudioRenderer&) = delete;

  void Render(std::span<float> output) noexcept;

  void SetVolume(float volume);
  void SetMuted(bool muted);
  void Pause();
  void Resume();
  void Flush();

  [[nodiscard]] bool IsPaused() const;
  [[nodiscard]] int64_t MediaFramesDelivered() const;
  [[nodiscard]] int64_t UnderrunFrames() const;
  [[nodiscard]] uint64_t Generation() const;
  [[nodiscard]] int64_t EstimatePlayedFrames(int64_t now_nanoseconds,
                                             int64_t latency_frames) const;
  [[nodiscard]] int32_t SampleRate() const { return config_.sample_rate; }
  [[nodiscard]] int32_t ChannelCount() const { return config_.channel_count; }

  static constexpr size_t kHistorySlots = 64;

 private:
  struct Slot {
    std::atomic<int64_t> device_end{0};
    std::atomic<int64_t> media_end{0};
    std::atomic<int64_t> time_nanoseconds{0};
  };

  AudioRenderer(const AudioRendererConfig& config,
                std::shared_ptr<AudioSource> source,
                const MonotonicClock* clock, size_t sample_bytes);

  void RenderChunk(std::span<float> output) noexcept;
  void Record(int64_t device_frames, int64_t media_frames) noexcept;
  [[nodiscard]] float ReadSample(std::span<const uint8_t> bytes) const noexcept;

  AudioRendererConfig config_;
  std::shared_ptr<AudioSource> source_;
  const MonotonicClock* clock_;
  size_t sample_bytes_;
  size_t frame_bytes_;
  float ramp_step_;
  std::vector<uint8_t> scratch_;
  // Bytes of a trailing partial frame carried into the next render call.
  std::vector<uint8_t> carry_;
  size_t carry_bytes_ = 0;

  std::atomic<float> volume_{1.0F};
  std::atomic<bool> muted_{false};
  std::atomic<bool> paused_{false};
  std::atomic<bool> flush_gate_{false};
  std::atomic<bool> in_render_{false};
  std::atomic<uint64_t> generation_{0};

  float volume_gain_ = 1.0F;
  float pause_gain_ = 0.0F;

  std::atomic<int64_t> device_total_{0};
  std::atomic<int64_t> media_total_{0};
  std::atomic<int64_t> underrun_frames_{0};
  std::atomic<uint64_t> published_{0};
  std::vector<Slot> history_;
};

}

#endif
