#ifndef PENUMBRA_DESKTOP_AUDIO_AUDIO_SINK_H_
#define PENUMBRA_DESKTOP_AUDIO_AUDIO_SINK_H_

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_events.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/desktop/audio/audio_renderer.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port.h"
#include "penumbra/interface/stream_info.h"

namespace penumbra::desktop {

enum class AudioRouteChange : uint8_t {
  kDeviceChanged,
  kFormatChanged,
};

struct AudioRoute {
  std::string driver;
  std::string device_name;
  int32_t sample_rate = 0;
  int32_t channel_count = 0;
  int32_t buffer_frames = 0;
  TimeDelta output_latency = TimeDelta::Unknown();
};

struct AudioPosition {
  int64_t media_frames_delivered = 0;
  int64_t media_frames_played = 0;
  TimeDelta played_time;
  TimeDelta output_latency = TimeDelta::Unknown();
  uint64_t flush_generation = 0;
};

class AudioSinkObserver {
 public:
  virtual ~AudioSinkObserver() = default;
  virtual void OnAudioRouteChanged(AudioRouteChange change,
                                   const AudioRoute& route) = 0;
  virtual void OnAudioError(const PortError& error) = 0;
};

struct AudioSinkOptions {
  std::string driver;
  std::string device_name;
  int32_t buffer_frames = 0;
  std::shared_ptr<LogSink> log;
  std::shared_ptr<AudioSinkObserver> observer;
  const MonotonicClock* clock = nullptr;
};

class DesktopAudioSink final : public AudioSink {
 public:
  explicit DesktopAudioSink(AudioSinkOptions options);
  ~DesktopAudioSink() override;
  DesktopAudioSink(const DesktopAudioSink&) = delete;
  DesktopAudioSink& operator=(const DesktopAudioSink&) = delete;

  absl::Status Start(AudioInfo format,
                     std::shared_ptr<AudioSource> audio) override;
  [[nodiscard]] absl::StatusOr<TimeDelta> GetOutputLatency() const override;
  absl::Status Stop() override;

  absl::Status Pause();
  absl::Status Resume();
  absl::Status Flush();
  absl::Status SetVolume(float volume);
  absl::Status SetMuted(bool muted);
  absl::Status ProcessDeviceEvents();

  [[nodiscard]] absl::StatusOr<AudioPosition> GetPosition() const;
  [[nodiscard]] absl::StatusOr<AudioRoute> GetRoute() const;

 private:
  struct DeviceEvents {
    static constexpr uint32_t kRemoved = 1;
    static constexpr uint32_t kFormatChanged = 2;
    std::atomic<uint32_t> pending{0};
    std::atomic<SDL_AudioDeviceID> physical_device{0};
    std::atomic<SDL_AudioDeviceID> logical_device{0};
  };

  struct CallbackState {
    AudioRenderer* renderer = nullptr;
    std::vector<float> scratch;
    size_t chunk_frames = 0;
    size_t channels = 0;
  };

  static void StreamCallback(void* userdata, SDL_AudioStream* stream,
                             int additional_amount, int total_amount);
  static bool EventWatch(void* userdata, SDL_Event* event);

  absl::Status OpenStream(bool use_default_device);
  void CloseStream();
  void Log(LogLevel level, const std::string& message) const;
  [[nodiscard]] absl::StatusOr<AudioRoute> QueryRoute() const;
  [[nodiscard]] absl::StatusOr<int64_t> LatencyFrames() const;
  absl::Status Reopen();
  void ReportError(const absl::Status& status);

  AudioSinkOptions options_;
  const MonotonicClock* clock_;
  AudioInfo format_;
  std::unique_ptr<AudioRenderer> renderer_;
  CallbackState callback_;
  DeviceEvents events_;
  SDL_AudioStream* stream_ = nullptr;
  SDL_AudioDeviceID requested_device_ = 0;
  bool started_ = false;
  bool stopped_ = false;
  bool audio_subsystem_ = false;
  bool watch_installed_ = false;
};

}

#endif
