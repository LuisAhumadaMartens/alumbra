#include "penumbra/desktop/audio/audio_renderer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/stream_info.h"

namespace penumbra::desktop {
namespace {

static_assert(std::atomic<float>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);
static_assert(std::atomic<int64_t>::is_always_lock_free);
static_assert(std::atomic<uint64_t>::is_always_lock_free);

constexpr int64_t kNanosecondsPerSecond = 1'000'000'000;
constexpr int64_t kMaxExtrapolationNanoseconds = 10 * kNanosecondsPerSecond;
constexpr int kEstimateAttempts = 4;
constexpr int32_t kMaxChannels = 64;

class SteadyMonotonicClock final : public MonotonicClock {
 public:
  [[nodiscard]] int64_t NowNanoseconds() const noexcept override {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }
};

float Approach(float value, float target, float step) {
  if (value < target) {
    return std::min(value + step, target);
  }
  return std::max(value - step, target);
}

size_t SampleBytes(SampleFormat format) {
  switch (format) {
    case SampleFormat::kF32:
    case SampleFormat::kS32:
      return 4;
    case SampleFormat::kS16:
      return 2;
    default:
      return 0;
  }
}

}

const MonotonicClock& SteadyClock() {
  static const SteadyMonotonicClock clock;
  return clock;
}

absl::StatusOr<std::unique_ptr<AudioRenderer>> AudioRenderer::Create(
    const AudioRendererConfig& config, std::shared_ptr<AudioSource> source,
    const MonotonicClock* clock) {
  if (source == nullptr || clock == nullptr) {
    return absl::InvalidArgumentError("An audio renderer needs a source");
  }
  if (config.sample_rate <= 0 || config.channel_count <= 0 ||
      config.channel_count > kMaxChannels || config.max_chunk_frames == 0 ||
      config.ramp_milliseconds < 0) {
    return absl::InvalidArgumentError("Invalid audio format");
  }
  if (config.sample_format == SampleFormat::kUnknown) {
    return absl::InvalidArgumentError("The sample format is unknown");
  }
  const size_t sample_bytes = SampleBytes(config.sample_format);
  if (sample_bytes == 0) {
    return absl::UnimplementedError(
        "Only interleaved 16-bit, 32-bit and float samples are supported");
  }
  return std::unique_ptr<AudioRenderer>(
      new AudioRenderer(config, std::move(source), clock, sample_bytes));
}

AudioRenderer::AudioRenderer(const AudioRendererConfig& config,
                             std::shared_ptr<AudioSource> source,
                             const MonotonicClock* clock, size_t sample_bytes)
    : config_(config),
      source_(std::move(source)),
      clock_(clock),
      sample_bytes_(sample_bytes),
      frame_bytes_(sample_bytes * static_cast<size_t>(config.channel_count)),
      ramp_step_(1.0F / static_cast<float>(std::max<int64_t>(
                            1, static_cast<int64_t>(config.sample_rate) *
                                   config.ramp_milliseconds / 1000))),
      scratch_(config.max_chunk_frames * frame_bytes_),
      carry_(frame_bytes_),
      history_(kHistorySlots) {}

float AudioRenderer::ReadSample(std::span<const uint8_t> bytes) const noexcept {
  switch (config_.sample_format) {
    case SampleFormat::kS16: {
      int16_t value = 0;
      std::memcpy(&value, bytes.data(), sizeof(value));
      return static_cast<float>(value) / 32768.0F;
    }
    case SampleFormat::kS32: {
      int32_t value = 0;
      std::memcpy(&value, bytes.data(), sizeof(value));
      return static_cast<float>(value) / 2147483648.0F;
    }
    default: {
      float value = 0.0F;
      std::memcpy(&value, bytes.data(), sizeof(value));
      return value;
    }
  }
}

void AudioRenderer::Render(std::span<float> output) noexcept {
  const auto channels = static_cast<size_t>(config_.channel_count);
  in_render_.store(true, std::memory_order_seq_cst);
  if (flush_gate_.load(std::memory_order_seq_cst)) {
    std::ranges::fill(output, 0.0F);
    in_render_.store(false, std::memory_order_release);
    return;
  }
  const size_t frames = output.size() / channels;
  size_t done = 0;
  while (done < frames) {
    const size_t chunk = std::min(frames - done, config_.max_chunk_frames);
    RenderChunk(output.subspan(done * channels, chunk * channels));
    done += chunk;
  }
  in_render_.store(false, std::memory_order_release);
}

void AudioRenderer::RenderChunk(std::span<float> output) noexcept {
  const auto channels = static_cast<size_t>(config_.channel_count);
  const size_t frames = output.size() / channels;
  const bool want_paused = paused_.load(std::memory_order_relaxed);
  const float volume_target = muted_.load(std::memory_order_relaxed)
                                  ? 0.0F
                                  : volume_.load(std::memory_order_relaxed);
  size_t readable = frames;
  if (want_paused) {
    readable = std::min(
        frames, static_cast<size_t>(std::lround(pause_gain_ / ramp_step_)));
  }
  size_t media_frames = 0;
  if (readable > 0) {
    const size_t wanted = readable * frame_bytes_;
    // A partial frame left by the previous read leads this one.
    std::memcpy(scratch_.data(), carry_.data(), carry_bytes_);
    const size_t read =
        std::min(source_->Read(std::span<uint8_t>(
                     scratch_.data() + carry_bytes_, wanted - carry_bytes_)),
                 wanted - carry_bytes_);
    const size_t got = carry_bytes_ + read;
    media_frames = got / frame_bytes_;
    carry_bytes_ = got - (media_frames * frame_bytes_);
    std::memcpy(carry_.data(), scratch_.data() + (media_frames * frame_bytes_),
                carry_bytes_);
  }
  const float pause_target = want_paused ? 0.0F : 1.0F;
  const std::span<const uint8_t> samples(scratch_);
  for (size_t frame = 0; frame < media_frames; ++frame) {
    volume_gain_ = Approach(volume_gain_, volume_target, ramp_step_);
    pause_gain_ = Approach(pause_gain_, pause_target, ramp_step_);
    const float gain = volume_gain_ * pause_gain_;
    for (size_t channel = 0; channel < channels; ++channel) {
      const size_t index = (frame * channels) + channel;
      output[index] =
          ReadSample(samples.subspan(index * sample_bytes_, sample_bytes_)) *
          gain;
    }
  }
  std::fill(
      output.begin() + static_cast<std::ptrdiff_t>(media_frames * channels),
      output.end(), 0.0F);
  if (!want_paused && media_frames < frames) {
    underrun_frames_.fetch_add(static_cast<int64_t>(frames - media_frames),
                               std::memory_order_relaxed);
  }
  Record(static_cast<int64_t>(frames), static_cast<int64_t>(media_frames));
}

void AudioRenderer::Record(int64_t device_frames,
                           int64_t media_frames) noexcept {
  const int64_t device_end =
      device_total_.load(std::memory_order_relaxed) + device_frames;
  const int64_t media_end =
      media_total_.load(std::memory_order_relaxed) + media_frames;
  device_total_.store(device_end, std::memory_order_release);
  media_total_.store(media_end, std::memory_order_release);
  const uint64_t sequence = published_.load(std::memory_order_relaxed);
  Slot& slot = history_[sequence % kHistorySlots];
  slot.device_end.store(device_end, std::memory_order_relaxed);
  slot.media_end.store(media_end, std::memory_order_relaxed);
  slot.time_nanoseconds.store(clock_->NowNanoseconds(),
                              std::memory_order_relaxed);
  published_.store(sequence + 1, std::memory_order_release);
}

void AudioRenderer::SetVolume(float volume) {
  volume_.store(std::clamp(volume, 0.0F, 1.0F), std::memory_order_relaxed);
}

void AudioRenderer::SetMuted(bool muted) {
  muted_.store(muted, std::memory_order_relaxed);
}

void AudioRenderer::Pause() { paused_.store(true, std::memory_order_relaxed); }

void AudioRenderer::Resume() {
  paused_.store(false, std::memory_order_relaxed);
}

bool AudioRenderer::IsPaused() const {
  return paused_.load(std::memory_order_relaxed);
}

void AudioRenderer::Flush() {
  flush_gate_.store(true, std::memory_order_seq_cst);
  while (in_render_.load(std::memory_order_seq_cst)) {
    std::this_thread::yield();
  }
  device_total_.store(0, std::memory_order_relaxed);
  media_total_.store(0, std::memory_order_relaxed);
  published_.store(0, std::memory_order_relaxed);
  pause_gain_ = 0.0F;
  carry_bytes_ = 0;
  generation_.fetch_add(1, std::memory_order_relaxed);
  flush_gate_.store(false, std::memory_order_seq_cst);
}

int64_t AudioRenderer::MediaFramesDelivered() const {
  return media_total_.load(std::memory_order_acquire);
}

int64_t AudioRenderer::UnderrunFrames() const {
  return underrun_frames_.load(std::memory_order_relaxed);
}

uint64_t AudioRenderer::Generation() const {
  return generation_.load(std::memory_order_relaxed);
}

int64_t AudioRenderer::EstimatePlayedFrames(int64_t now_nanoseconds,
                                            int64_t latency_frames) const {
  int64_t media = 0;
  for (int attempt = 0; attempt < kEstimateAttempts; ++attempt) {
    const uint64_t head = published_.load(std::memory_order_acquire);
    if (head == 0) {
      return 0;
    }
    const Slot& newest = history_[(head - 1) % kHistorySlots];
    const int64_t device_end =
        newest.device_end.load(std::memory_order_relaxed);
    const int64_t elapsed_nanoseconds = std::clamp<int64_t>(
        now_nanoseconds -
            newest.time_nanoseconds.load(std::memory_order_relaxed),
        0, kMaxExtrapolationNanoseconds);
    const int64_t elapsed_frames =
        elapsed_nanoseconds * config_.sample_rate / kNanosecondsPerSecond;
    const int64_t heard = std::clamp<int64_t>(
        device_end - latency_frames + elapsed_frames, 0, device_end);

    const uint64_t oldest =
        head > kHistorySlots - 2 ? head - (kHistorySlots - 3) : 1;
    media = 0;
    for (uint64_t sequence = head; sequence >= oldest; --sequence) {
      const Slot& current = history_[(sequence - 1) % kHistorySlots];
      int64_t previous_device = 0;
      int64_t previous_media = 0;
      if (sequence > 1) {
        const Slot& previous = history_[(sequence - 2) % kHistorySlots];
        previous_device = previous.device_end.load(std::memory_order_relaxed);
        previous_media = previous.media_end.load(std::memory_order_relaxed);
      }
      media = previous_media;
      if (heard >= previous_device) {
        const int64_t current_media =
            current.media_end.load(std::memory_order_relaxed);
        media = previous_media + std::min(heard - previous_device,
                                          current_media - previous_media);
        break;
      }
      if (sequence == 1) {
        break;
      }
    }
    if (published_.load(std::memory_order_acquire) - head < 2) {
      return media;
    }
  }
  return media;
}

}
