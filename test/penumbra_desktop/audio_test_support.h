#ifndef TEST_PENUMBRA_DESKTOP_AUDIO_TEST_SUPPORT_H_
#define TEST_PENUMBRA_DESKTOP_AUDIO_TEST_SUPPORT_H_

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "penumbra/desktop/audio/audio_renderer.h"
#include "penumbra/desktop/audio/audio_sink.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port.h"
#include "penumbra/interface/port_types.h"

namespace penumbra::desktop::testing {

class ManualClock final : public MonotonicClock {
 public:
  [[nodiscard]] int64_t NowNanoseconds() const noexcept override {
    return now_.load(std::memory_order_relaxed);
  }
  void Set(int64_t nanoseconds) {
    now_.store(nanoseconds, std::memory_order_relaxed);
  }

 private:
  std::atomic<int64_t> now_{0};
};

class ScriptedSource final : public AudioSource {
 public:
  enum class Mode : int { kCounter, kConstant, kSilentGate };

  ScriptedSource(int32_t channels, size_t sample_bytes)
      : channels_(static_cast<size_t>(channels)), sample_bytes_(sample_bytes) {}

  size_t Read(std::span<uint8_t> destination) noexcept override {
    const Mode mode = mode_.load(std::memory_order_acquire);
    if (mode == Mode::kSilentGate) {
      return 0;
    }
    const size_t frame_bytes = channels_ * sample_bytes_;
    size_t available = destination.size() / frame_bytes;
    const int64_t limit = frame_limit_.load(std::memory_order_relaxed);
    const int64_t produced = frames_read_.load(std::memory_order_relaxed);
    if (limit >= 0) {
      available =
          std::min(available,
                   static_cast<size_t>(std::max<int64_t>(0, limit - produced)));
    }
    for (size_t frame = 0; frame < available; ++frame) {
      const float value =
          mode == Mode::kCounter
              ? static_cast<float>(produced + static_cast<int64_t>(frame))
              : constant_.load(std::memory_order_relaxed);
      for (size_t channel = 0; channel < channels_; ++channel) {
        const std::span<uint8_t> target = destination.subspan(
            (frame * frame_bytes) + (channel * sample_bytes_), sample_bytes_);
        if (sample_bytes_ == 2) {
          const auto sample = static_cast<int16_t>(value * 32767.0F);
          std::memcpy(target.data(), &sample, sizeof(sample));
        } else {
          std::memcpy(target.data(), &value, sizeof(value));
        }
      }
    }
    frames_read_.fetch_add(static_cast<int64_t>(available),
                           std::memory_order_relaxed);
    return available * frame_bytes;
  }

  void SetMode(Mode mode) { mode_.store(mode, std::memory_order_release); }
  void SetConstant(float value) {
    constant_.store(value, std::memory_order_relaxed);
  }
  void SetFrameLimit(int64_t frames) {
    frame_limit_.store(frames, std::memory_order_relaxed);
  }
  [[nodiscard]] int64_t FramesRead() const {
    return frames_read_.load(std::memory_order_relaxed);
  }

 private:
  size_t channels_;
  size_t sample_bytes_;
  std::atomic<Mode> mode_{Mode::kConstant};
  std::atomic<float> constant_{0.5F};
  std::atomic<int64_t> frame_limit_{-1};
  std::atomic<int64_t> frames_read_{0};
};

class RecordingLog final : public LogSink {
 public:
  absl::Status Log(LogLevel level, std::string_view message) override {
    const std::scoped_lock lock(mutex_);
    messages_.emplace_back(level, std::string(message));
    return absl::OkStatus();
  }

  [[nodiscard]] bool Contains(std::string_view text) const {
    const std::scoped_lock lock(mutex_);
    return std::ranges::any_of(messages_, [text](const auto& entry) {
      return entry.second.find(text) != std::string::npos;
    });
  }

 private:
  mutable std::mutex mutex_;
  std::vector<std::pair<LogLevel, std::string>> messages_;
};

class RecordingObserver final : public AudioSinkObserver {
 public:
  void OnAudioRouteChanged(AudioRouteChange change,
                           const AudioRoute& route) override {
    changes.emplace_back(change, route);
  }
  void OnAudioError(const PortError& error) override {
    errors.push_back(error);
  }

  std::vector<std::pair<AudioRouteChange, AudioRoute>> changes;
  std::vector<PortError> errors;
};

template <typename Predicate>
bool WaitUntil(Predicate predicate,
               std::chrono::milliseconds timeout = std::chrono::seconds(10)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!predicate()) {
    if (std::chrono::steady_clock::now() > deadline) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return true;
}

}

#endif
