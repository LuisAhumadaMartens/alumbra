#include "penumbra/desktop/audio/audio_sink.h"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "penumbra/desktop/audio/audio_renderer.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/stream_info.h"

namespace penumbra::desktop {
namespace {

constexpr size_t kChunkFrames = 4096;
constexpr int32_t kMaxDeviceChannels = 8;

TimeDelta FramesToTime(int64_t frames, int32_t sample_rate) {
  return TimeDelta::FromMicroseconds(frames * 1'000'000 / sample_rate);
}

}

DesktopAudioSink::DesktopAudioSink(AudioSinkOptions options)
    : options_(std::move(options)),
      clock_(options_.clock != nullptr ? options_.clock : &SteadyClock()) {}

DesktopAudioSink::~DesktopAudioSink() { static_cast<void>(Stop()); }

void DesktopAudioSink::Log(LogLevel level, const std::string& message) const {
  if (options_.log != nullptr) {
    static_cast<void>(options_.log->Log(level, message));
  }
}

void DesktopAudioSink::StreamCallback(void* userdata, SDL_AudioStream* stream,
                                      int additional_amount, int total_amount) {
  static_cast<void>(total_amount);
  auto* state = static_cast<CallbackState*>(userdata);
  const size_t frame_bytes = state->channels * sizeof(float);
  size_t remaining =
      static_cast<size_t>(std::max(additional_amount, 0)) / frame_bytes;
  while (remaining > 0) {
    const size_t frames = std::min(remaining, state->chunk_frames);
    state->renderer->Render(
        std::span<float>(state->scratch.data(), frames * state->channels));
    if (!SDL_PutAudioStreamData(stream, state->scratch.data(),
                                static_cast<int>(frames * frame_bytes))) {
      return;
    }
    remaining -= frames;
  }
}

bool DesktopAudioSink::EventWatch(void* userdata, SDL_Event* event) {
  auto* events = static_cast<DeviceEvents*>(userdata);
  const SDL_AudioDeviceID physical =
      events->physical_device.load(std::memory_order_relaxed);
  const SDL_AudioDeviceID logical =
      events->logical_device.load(std::memory_order_relaxed);
  if (event->type == SDL_EVENT_AUDIO_DEVICE_REMOVED && physical != 0 &&
      event->adevice.which == physical) {
    events->pending.fetch_or(DeviceEvents::kRemoved, std::memory_order_release);
  } else if (event->type == SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED &&
             logical != 0 &&
             (event->adevice.which == logical ||
              event->adevice.which == physical)) {
    events->pending.fetch_or(DeviceEvents::kFormatChanged,
                             std::memory_order_release);
  }
  return true;
}

absl::Status DesktopAudioSink::Start(AudioInfo format,
                                     std::shared_ptr<AudioSource> audio) {
  if (started_ || stopped_) {
    return absl::FailedPreconditionError(
        "The audio sink is already started or stopped");
  }
  if (audio == nullptr) {
    return absl::InvalidArgumentError("An audio sink needs an audio source");
  }
  AudioRendererConfig config;
  config.sample_rate = format.sample_rate;
  config.channel_count = format.channel_count;
  config.sample_format = format.sample_format;
  config.max_chunk_frames = kChunkFrames;
  absl::StatusOr<std::unique_ptr<AudioRenderer>> renderer =
      AudioRenderer::Create(config, std::move(audio), clock_);
  if (!renderer.ok()) {
    return renderer.status();
  }
  if (format.channel_count > kMaxDeviceChannels) {
    return absl::UnimplementedError(
        "The desktop audio sink supports at most 8 channels");
  }
  format_ = format;
  renderer_ = *std::move(renderer);
  callback_.renderer = renderer_.get();
  callback_.chunk_frames = kChunkFrames;
  callback_.channels = static_cast<size_t>(format.channel_count);
  callback_.scratch.assign(kChunkFrames * callback_.channels, 0.0F);

  if (!options_.driver.empty()) {
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, options_.driver.c_str());
  }
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    renderer_.reset();
    return absl::UnavailableError(
        absl::StrCat("SDL_InitSubSystem(audio): ", SDL_GetError()));
  }
  audio_subsystem_ = true;
  watch_installed_ = SDL_AddEventWatch(&EventWatch, &events_);
  absl::Status opened = OpenStream(options_.device_name.empty());
  if (!opened.ok()) {
    if (watch_installed_) {
      SDL_RemoveEventWatch(&EventWatch, &events_);
      watch_installed_ = false;
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    audio_subsystem_ = false;
    renderer_.reset();
    return opened;
  }
  started_ = true;
  const absl::StatusOr<AudioRoute> route = QueryRoute();
  if (route.ok()) {
    Log(LogLevel::kInfo,
        absl::StrCat("Audio output opened: driver ", route->driver, ", device ",
                     route->device_name, ", ", route->sample_rate, " Hz, ",
                     route->channel_count, " channels, ", route->buffer_frames,
                     " frame buffer, output latency ",
                     route->output_latency.InMicroseconds(), " us"));
  }
  return absl::OkStatus();
}

absl::Status DesktopAudioSink::OpenStream(bool use_default_device) {
  SDL_AudioDeviceID device = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
  events_.physical_device.store(0, std::memory_order_relaxed);
  if (!use_default_device) {
    int count = 0;
    SDL_AudioDeviceID* devices = SDL_GetAudioPlaybackDevices(&count);
    device = 0;
    if (devices != nullptr) {
      for (const SDL_AudioDeviceID candidate :
           std::span<const SDL_AudioDeviceID>(devices,
                                              static_cast<size_t>(count))) {
        const char* name = SDL_GetAudioDeviceName(candidate);
        if (name != nullptr && options_.device_name == name) {
          device = candidate;
          break;
        }
      }
      SDL_free(devices);
    }
    if (device == 0) {
      return absl::NotFoundError(absl::StrCat(
          "No audio output device is named \"", options_.device_name, "\""));
    }
    events_.physical_device.store(device, std::memory_order_relaxed);
  }
  if (options_.buffer_frames > 0) {
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES,
                std::to_string(options_.buffer_frames).c_str());
  }
  SDL_AudioSpec spec;
  spec.format = SDL_AUDIO_F32;
  spec.channels = format_.channel_count;
  spec.freq = format_.sample_rate;
  SDL_AudioStream* stream =
      SDL_OpenAudioDeviceStream(device, &spec, &StreamCallback, &callback_);
  if (stream == nullptr) {
    return absl::UnavailableError(
        absl::StrCat("SDL_OpenAudioDeviceStream: ", SDL_GetError()));
  }
  stream_ = stream;
  requested_device_ = device;
  events_.logical_device.store(SDL_GetAudioStreamDevice(stream),
                               std::memory_order_relaxed);
  if (!SDL_ResumeAudioStreamDevice(stream)) {
    const std::string error = SDL_GetError();
    CloseStream();
    return absl::UnavailableError(
        absl::StrCat("SDL_ResumeAudioStreamDevice: ", error));
  }
  return absl::OkStatus();
}

void DesktopAudioSink::CloseStream() {
  if (stream_ != nullptr) {
    SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
  }
  events_.logical_device.store(0, std::memory_order_relaxed);
}

absl::Status DesktopAudioSink::Stop() {
  if (!started_) {
    return absl::OkStatus();
  }
  CloseStream();
  if (watch_installed_) {
    SDL_RemoveEventWatch(&EventWatch, &events_);
    watch_installed_ = false;
  }
  if (audio_subsystem_) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    audio_subsystem_ = false;
  }
  renderer_.reset();
  callback_.renderer = nullptr;
  started_ = false;
  stopped_ = true;
  return absl::OkStatus();
}

absl::StatusOr<int64_t> DesktopAudioSink::LatencyFrames() const {
  SDL_AudioSpec device_spec;
  int device_frames = 0;
  if (!SDL_GetAudioDeviceFormat(events_.logical_device.load(), &device_spec,
                                &device_frames) ||
      device_spec.freq <= 0) {
    return absl::UnavailableError(
        absl::StrCat("SDL_GetAudioDeviceFormat: ", SDL_GetError()));
  }
  int64_t frames = static_cast<int64_t>(device_frames) * format_.sample_rate /
                   device_spec.freq;
  const int queued_bytes = SDL_GetAudioStreamQueued(stream_);
  if (queued_bytes > 0) {
    frames +=
        queued_bytes / static_cast<int64_t>(sizeof(float) * callback_.channels);
  }
  return frames;
}

absl::StatusOr<TimeDelta> DesktopAudioSink::GetOutputLatency() const {
  if (!started_ || stream_ == nullptr) {
    return TimeDelta::Unknown();
  }
  const absl::StatusOr<int64_t> frames = LatencyFrames();
  if (!frames.ok()) {
    return frames.status();
  }
  return FramesToTime(*frames, format_.sample_rate);
}

absl::StatusOr<AudioRoute> DesktopAudioSink::QueryRoute() const {
  if (stream_ == nullptr) {
    return absl::FailedPreconditionError("No audio device is open");
  }
  SDL_AudioSpec device_spec;
  int device_frames = 0;
  if (!SDL_GetAudioDeviceFormat(events_.logical_device.load(), &device_spec,
                                &device_frames)) {
    return absl::UnavailableError(
        absl::StrCat("SDL_GetAudioDeviceFormat: ", SDL_GetError()));
  }
  AudioRoute route;
  const char* driver = SDL_GetCurrentAudioDriver();
  route.driver = driver != nullptr ? driver : "";
  const char* name = SDL_GetAudioDeviceName(requested_device_);
  route.device_name = name != nullptr ? name : "";
  route.sample_rate = device_spec.freq;
  route.channel_count = device_spec.channels;
  route.buffer_frames = device_frames;
  const absl::StatusOr<int64_t> latency = LatencyFrames();
  if (latency.ok()) {
    route.output_latency = FramesToTime(*latency, format_.sample_rate);
  }
  return route;
}

absl::StatusOr<AudioRoute> DesktopAudioSink::GetRoute() const {
  if (!started_) {
    return absl::FailedPreconditionError("The audio sink is not started");
  }
  return QueryRoute();
}

absl::StatusOr<AudioPosition> DesktopAudioSink::GetPosition() const {
  if (!started_) {
    return absl::FailedPreconditionError("The audio sink is not started");
  }
  AudioPosition position;
  position.flush_generation = renderer_->Generation();
  int64_t latency_frames = 0;
  if (stream_ != nullptr) {
    const absl::StatusOr<int64_t> latency = LatencyFrames();
    if (!latency.ok()) {
      return latency.status();
    }
    latency_frames = *latency;
    position.output_latency = FramesToTime(latency_frames, format_.sample_rate);
  }
  position.media_frames_played =
      renderer_->EstimatePlayedFrames(clock_->NowNanoseconds(), latency_frames);
  position.media_frames_delivered = renderer_->MediaFramesDelivered();
  position.played_time =
      FramesToTime(position.media_frames_played, format_.sample_rate);
  return position;
}

absl::Status DesktopAudioSink::Pause() {
  if (!started_) {
    return absl::FailedPreconditionError("The audio sink is not started");
  }
  renderer_->Pause();
  return absl::OkStatus();
}

absl::Status DesktopAudioSink::Resume() {
  if (!started_) {
    return absl::FailedPreconditionError("The audio sink is not started");
  }
  renderer_->Resume();
  return absl::OkStatus();
}

absl::Status DesktopAudioSink::Flush() {
  if (!started_) {
    return absl::FailedPreconditionError("The audio sink is not started");
  }
  if (stream_ == nullptr) {
    renderer_->Flush();
    return absl::OkStatus();
  }
  SDL_PauseAudioStreamDevice(stream_);
  renderer_->Flush();
  SDL_ClearAudioStream(stream_);
  if (!SDL_ResumeAudioStreamDevice(stream_)) {
    return absl::UnavailableError(
        absl::StrCat("SDL_ResumeAudioStreamDevice: ", SDL_GetError()));
  }
  return absl::OkStatus();
}

absl::Status DesktopAudioSink::SetVolume(float volume) {
  if (!started_) {
    return absl::FailedPreconditionError("The audio sink is not started");
  }
  if (std::isnan(volume) || volume < 0.0F || volume > 1.0F) {
    return absl::InvalidArgumentError("Volume must be between 0 and 1");
  }
  renderer_->SetVolume(volume);
  return absl::OkStatus();
}

absl::Status DesktopAudioSink::SetMuted(bool muted) {
  if (!started_) {
    return absl::FailedPreconditionError("The audio sink is not started");
  }
  renderer_->SetMuted(muted);
  return absl::OkStatus();
}

void DesktopAudioSink::ReportError(const absl::Status& status) {
  Log(LogLevel::kError, std::string(status.message()));
  if (options_.observer != nullptr) {
    options_.observer->OnAudioError(PortError{
        .status = status,
        .category = PortErrorCategory::kCapability,
        .recovery = RecoveryHint::kRecreate,
    });
  }
}

absl::Status DesktopAudioSink::Reopen() {
  CloseStream();
  const absl::Status opened = OpenStream(true);
  if (!opened.ok()) {
    const absl::Status error = absl::UnavailableError(absl::StrCat(
        "capability changed: the audio output device was removed and no "
        "replacement could be opened: ",
        opened.message()));
    ReportError(error);
    return error;
  }
  const absl::StatusOr<AudioRoute> route = QueryRoute();
  if (route.ok()) {
    Log(LogLevel::kWarning,
        absl::StrCat("Audio output device removed; reopened on ",
                     route->device_name, ", output latency ",
                     route->output_latency.InMicroseconds(), " us"));
    if (options_.observer != nullptr) {
      options_.observer->OnAudioRouteChanged(AudioRouteChange::kDeviceChanged,
                                             *route);
    }
  }
  return absl::OkStatus();
}

absl::Status DesktopAudioSink::ProcessDeviceEvents() {
  if (!started_) {
    return absl::FailedPreconditionError("The audio sink is not started");
  }
  const uint32_t pending =
      events_.pending.exchange(0, std::memory_order_acquire);
  if ((pending & DeviceEvents::kRemoved) != 0) {
    return Reopen();
  }
  if ((pending & DeviceEvents::kFormatChanged) != 0 && stream_ != nullptr) {
    const absl::StatusOr<AudioRoute> route = QueryRoute();
    if (!route.ok()) {
      return route.status();
    }
    Log(LogLevel::kInfo,
        absl::StrCat("Audio output format changed; output latency ",
                     route->output_latency.InMicroseconds(), " us"));
    if (options_.observer != nullptr) {
      options_.observer->OnAudioRouteChanged(AudioRouteChange::kFormatChanged,
                                             *route);
    }
  }
  return absl::OkStatus();
}

}
