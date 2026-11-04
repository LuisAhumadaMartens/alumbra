#include "penumbra/desktop/desktop_port.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <thread>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/time/time.h"
#include "penumbra/desktop/audio/audio_sink.h"
#include "penumbra/desktop/audio_converter.h"
#include "penumbra/desktop/capability_provider.h"
#include "penumbra/desktop/curl_http_transport.h"
#include "penumbra/desktop/desktop_component_factory.h"
#include "penumbra/desktop/ffmpeg_fallback_demuxer.h"
#include "penumbra/desktop/file_byte_stream_provider.h"
#include "penumbra/desktop/software_decoder_probe.h"
#include "penumbra/desktop/stderr_log_sink.h"
#include "penumbra/desktop/video/gpu_presenter.h"
#include "penumbra/desktop/video/presenter.h"
#include "penumbra/desktop/video/video_geometry.h"
#include "penumbra/desktop/video/video_sink.h"
#include "penumbra/interface/callback_dispatcher.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/decoder_types.h"
#include "penumbra/interface/default_port.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/fallback_demuxer.h"
#include "penumbra/interface/http_transport.h"
#include "penumbra/interface/interface.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/stream_info.h"
#include "penumbra/interface/stream_provider.h"
#include "penumbra/interface/task_runner.h"
#include "penumbra/reference_player/audio_converter_provider.h"
#include "penumbra/reference_player/player.h"

namespace penumbra::desktop {
namespace {

struct DefaultSinkFactories {
  std::mutex mutex;
  std::function<absl::StatusOr<std::unique_ptr<AudioSink>>()> audio;
  std::function<absl::StatusOr<std::unique_ptr<VideoSink>>(OutputMode)> video;
};

DefaultSinkFactories& DefaultSinkFactoriesState() {
  static DefaultSinkFactories factories;
  return factories;
}

class DesktopTaskRunner final : public ControlTaskRunner {
 public:
  DesktopTaskRunner() : worker_([this] { Run(); }) {}
  DesktopTaskRunner(const DesktopTaskRunner&) = delete;
  DesktopTaskRunner& operator=(const DesktopTaskRunner&) = delete;

  ~DesktopTaskRunner() override {
    {
      const std::scoped_lock lock(mutex_);
      stopping_ = true;
    }
    condition_.notify_all();
    worker_.join();
  }

  absl::Status PostTask(Task task, absl::Duration delay) override {
    if (task == nullptr) {
      return absl::InvalidArgumentError("a task runner needs a task");
    }
    {
      const std::scoped_lock lock(mutex_);
      if (stopping_) {
        return absl::FailedPreconditionError("the task runner has stopped");
      }
      const auto nonnegative = std::max(delay, absl::ZeroDuration());
      tasks_.push_back({.due = std::chrono::steady_clock::now() +
                               absl::ToChronoNanoseconds(nonnegative),
                        .order = next_order_++,
                        .task = std::move(task)});
    }
    condition_.notify_one();
    return absl::OkStatus();
  }

  bool RunsTasksInCurrentSequence() const override {
    const std::scoped_lock lock(mutex_);
    return std::this_thread::get_id() == worker_id_;
  }

 private:
  struct ScheduledTask {
    std::chrono::steady_clock::time_point due;
    uint64_t order;
    Task task;
  };

  void Run() {
    std::unique_lock lock(mutex_);
    worker_id_ = std::this_thread::get_id();
    while (true) {
      if (stopping_) {
        return;
      }
      if (tasks_.empty()) {
        condition_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
        continue;
      }
      const auto next = std::ranges::min_element(
          tasks_, [](const ScheduledTask& left, const ScheduledTask& right) {
            return left.due == right.due ? left.order < right.order
                                         : left.due < right.due;
          });
      const auto due = next->due;
      if (std::chrono::steady_clock::now() < due) {
        condition_.wait_until(lock, due);
        continue;
      }
      Task task = std::move(next->task);
      tasks_.erase(next);
      lock.unlock();
      std::move(task)();
      lock.lock();
    }
  }

  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::vector<ScheduledTask> tasks_;
  uint64_t next_order_ = 0;
  bool stopping_ = false;
  std::thread::id worker_id_;
  std::thread worker_;
};

class HeadlessVideoSink final : public VideoSink {
 public:
  absl::Status Start(std::shared_ptr<VideoFrameSource> frames) override {
    if (frames == nullptr) {
      return absl::InvalidArgumentError("a video sink needs frames");
    }
    if (started_ || stopped_) {
      return absl::FailedPreconditionError("video sink cannot be started");
    }
    frames_ = std::move(frames);
    started_ = true;
    return absl::OkStatus();
  }

  absl::Status SetBounds(OutputBounds bounds) override {
    if (bounds.width < 0 || bounds.height < 0) {
      return absl::InvalidArgumentError("video bounds cannot be negative");
    }
    bounds_ = bounds;
    return absl::OkStatus();
  }

  absl::Status Stop() override {
    frames_.reset();
    stopped_ = true;
    return absl::OkStatus();
  }

 private:
  std::shared_ptr<VideoFrameSource> frames_;
  OutputBounds bounds_;
  bool started_ = false;
  bool stopped_ = false;
};

class HttpByteStreamCallbacks final : public HttpTransportCallbacks {
 public:
  explicit HttpByteStreamCallbacks(
      std::shared_ptr<ByteStreamCallbacks> callbacks)
      : callbacks_(std::move(callbacks)) {}

  absl::Status OnResponse(HttpResponseHead head) override {
    const CallbackScope scope;
    if (head.status < 200 || head.status >= 300) {
      return absl::UnavailableError("HTTP byte stream request failed");
    }
    ByteStreamInfo info;
    info.seekable = head.status == 206;
    info.final_url = std::move(head.final_url);
    for (const HttpHeader& header : head.headers) {
      if (EqualAsciiInsensitive(header.name, "content-type")) {
        info.content_type = header.value;
      } else if (EqualAsciiInsensitive(header.name, "content-length")) {
        uint64_t size = 0;
        if (absl::SimpleAtoi(header.value, &size)) {
          info.size = size;
        }
      } else if (EqualAsciiInsensitive(header.name, "accept-ranges") &&
                 header.value == "bytes") {
        info.seekable = true;
      }
    }
    return callbacks_->OnOpen(std::move(info));
  }

  absl::Status OnChunk(HttpChunk chunk) override {
    const CallbackScope scope;
    return callbacks_->OnData(
        {.data = std::move(chunk.data), .arrival_time = chunk.arrival_time});
  }

  absl::Status OnComplete(HttpMetrics metrics) override {
    const CallbackScope scope;
    static_cast<void>(metrics);
    return callbacks_->OnEnd();
  }

  absl::Status OnError(absl::Status error) override {
    const CallbackScope scope;
    return callbacks_->OnError(std::move(error));
  }

 private:
  static bool EqualAsciiInsensitive(std::string_view left,
                                    std::string_view right) {
    if (left.size() != right.size()) {
      return false;
    }
    for (size_t index = 0; index < left.size(); ++index) {
      const char lhs = left[index] >= 'A' && left[index] <= 'Z'
                           ? static_cast<char>(left[index] - 'A' + 'a')
                           : left[index];
      const char rhs = right[index] >= 'A' && right[index] <= 'Z'
                           ? static_cast<char>(right[index] - 'A' + 'a')
                           : right[index];
      if (lhs != rhs) {
        return false;
      }
    }
    return true;
  }

  std::shared_ptr<ByteStreamCallbacks> callbacks_;
};

class HttpByteStream final : public ByteStream {
 public:
  explicit HttpByteStream(std::unique_ptr<HttpRequestHandle> handle)
      : handle_(std::move(handle)) {}

  void Cancel() override {
    const PortCallScope scope;
    handle_->Cancel();
  }
  absl::Status Pause() override {
    const PortCallScope scope;
    return handle_->Pause();
  }
  absl::Status Resume() override {
    const PortCallScope scope;
    return handle_->Resume();
  }

 private:
  std::unique_ptr<HttpRequestHandle> handle_;
};

class HttpByteStreamProvider final : public ByteStreamProvider {
 public:
  HttpByteStreamProvider(std::shared_ptr<HttpTransport> transport,
                         const DesktopPortOptions& options)
      : transport_(std::move(transport)),
        connect_timeout_ms_(options.http_connect_timeout_ms),
        max_redirects_(options.http_max_redirects),
        user_agent_(options.http_user_agent.empty() ? "Alumbra/0.0.0"
                                                    : options.http_user_agent),
        allow_https_downgrade_(options.http_allow_https_to_http_redirect) {}

  [[nodiscard]] std::vector<std::string> Schemes() const override {
    const PortCallScope scope;
    return {"http", "https"};
  }

  absl::StatusOr<std::unique_ptr<ByteStream>> Open(
      ByteStreamRequest request,
      std::shared_ptr<ByteStreamCallbacks> callbacks) override {
    const PortCallScope scope;
    if (callbacks == nullptr || !(request.url.starts_with("http://") ||
                                  request.url.starts_with("https://"))) {
      return absl::InvalidArgumentError(
          "HTTP byte stream requires an HTTP or HTTPS URL and callbacks");
    }
    if (request.credential.has_value()) {
      return absl::UnimplementedError(
          "HTTP byte stream credentials are not supported");
    }
    HttpRequest http_request;
    http_request.url = std::move(request.url);
    http_request.request_class = request.request_class;
    http_request.connect_timeout = absl::Milliseconds(connect_timeout_ms_);
    http_request.redirects = {.max_redirects = max_redirects_,
                              .allow_https_downgrade = allow_https_downgrade_};
    http_request.headers.push_back(
        {.name = "User-Agent", .value = user_agent_});
    if (request.offset != 0 || request.length.has_value()) {
      http_request.range =
          ByteRange{.offset = request.offset, .length = request.length};
    }
    auto http_callbacks =
        std::make_shared<HttpByteStreamCallbacks>(std::move(callbacks));
    auto handle =
        transport_->Start(std::move(http_request), std::move(http_callbacks));
    if (!handle.ok()) {
      return handle.status();
    }
    return std::unique_ptr<ByteStream>(
        std::make_unique<HttpByteStream>(std::move(*handle)));
  }

 private:
  std::shared_ptr<HttpTransport> transport_;
  int64_t connect_timeout_ms_;
  int32_t max_redirects_;
  std::string user_agent_;
  bool allow_https_downgrade_;
};

class DesktopPort final : public Port,
                          public ReferenceAudioConverterProvider,
                          public DesktopWindowControl {
 public:
  static absl::StatusOr<std::unique_ptr<DesktopPort>> Create(
      DesktopPortOptions options) {
    auto transport = CurlHttpTransport::Create(
        {.ca_bundle_path = std::move(options.ca_bundle_path)});
    if (!transport.ok()) {
      return transport.status();
    }
    return std::unique_ptr<DesktopPort>(
        new DesktopPort(std::move(options), std::move(*transport)));
  }

  ~DesktopPort() override { static_cast<void>(transport_->Stop()); }
  DesktopPort(const DesktopPort&) = delete;
  DesktopPort& operator=(const DesktopPort&) = delete;

  void SetControlRunner(std::shared_ptr<ControlTaskRunner> control) {
    control_runner_ = std::move(control);
  }

  void AttachWindow(SDL_Window* window,
                    std::function<void(GpuPresenter&)> on_presenter) override {
    window_ = window;
    on_presenter_ = std::move(on_presenter);
  }

  absl::Status SetControlTaskRunner(
      const std::shared_ptr<ControlTaskRunner>& runner) override {
    if (runner == nullptr) {
      return absl::InvalidArgumentError("a port needs a control task runner");
    }
    control_runner_ = runner;
    return absl::OkStatus();
  }

  absl::StatusOr<PortLimits> GetLimits() const override {
    const PortCallScope scope;
    return PortLimits{.api_version = kApiVersion, .max_players = 1};
  }

  absl::StatusOr<std::unique_ptr<Player>> CreatePlayer(
      PlayerConfig config,
      std::shared_ptr<PlayerCallbacks> callbacks) override {
    const PortCallScope scope;
    if (!active_player_.expired()) {
      return absl::ResourceExhaustedError("desktop player limit reached");
    }
    if (control_runner_ == nullptr) {
      return absl::FailedPreconditionError(
          "create a control task runner before creating a player");
    }
    config.decoder = options_.decoder_settings;
    auto player = CreateReferencePlayer(std::move(config), std::move(callbacks),
                                        *this, control_runner_);
    if (!player.ok()) {
      return player.status();
    }
    auto lease = std::make_shared<int>(0);
    active_player_ = lease;
    return std::unique_ptr<Player>(
        new LeasedPlayer(std::move(*player), std::move(lease)));
  }

  absl::StatusOr<DecodingCapability> QueryDecoding(
      const CapabilityRequest& request) const override {
    const PortCallScope scope;
    return capabilities_.QueryDecoding(request);
  }

  absl::StatusOr<bool> CanChangeType(const StreamConfig& from,
                                     const StreamConfig& to) const override {
    const PortCallScope scope;
    return capabilities_.CanChangeType(from, to);
  }

  absl::StatusOr<std::vector<DecoderCandidate>> GetDecoderCandidates(
      StreamType type) const override {
    const PortCallScope scope;
    return components_.GetDecoderCandidates(type);
  }

  absl::StatusOr<std::unique_ptr<Decoder>> CreateDecoder(
      const DecoderCandidate& candidate, StreamConfig config, SeekTicket ticket,
      std::shared_ptr<DecoderCallbacks> callbacks) override {
    const PortCallScope scope;
    return components_.CreateDecoder(candidate, std::move(config), ticket,
                                     std::move(callbacks));
  }

  absl::StatusOr<std::unique_ptr<VideoSink>> CreateVideoSink(
      OutputMode mode) override {
    const PortCallScope scope;
    if (options_.video_sink_factory) {
      return options_.video_sink_factory(mode);
    }
    if (mode != OutputMode::kTexture) {
      return absl::UnimplementedError(
          "the desktop port supports texture video output only");
    }
    if (options_.headless) {
      return std::unique_ptr<VideoSink>(std::make_unique<HeadlessVideoSink>());
    }
    if (window_ == nullptr && !options_.headless) {
      return absl::FailedPreconditionError(
          "a desktop window must be attached before loading video");
    }
    const GpuPresenterOptions presenter_options{
        .window = window_,
        .offscreen_logical_size = {.width = 640, .height = 360},
        .offscreen_pixel_density = 1.0F,
        .scaling_filter = ScalingFilter::kLinear,
        .frames_in_flight = 1,
        .debug_mode = false};
    auto presenter = GpuPresenter::Create(presenter_options);
    if (!presenter.ok()) {
      return presenter.status();
    }
    if (on_presenter_) {
      on_presenter_(**presenter);
    }
    std::shared_ptr<Presenter> shared_presenter(std::move(*presenter));
    return std::unique_ptr<VideoSink>(
        std::make_unique<DesktopVideoSink>(std::move(shared_presenter)));
  }

  absl::StatusOr<std::unique_ptr<AudioSink>> CreateAudioSink() override {
    const PortCallScope scope;
    if (options_.audio_sink_factory) {
      return options_.audio_sink_factory();
    }
    AudioSinkOptions sink_options;
    sink_options.device_name = options_.audio_output_device;
    if (options_.headless) {
      sink_options.driver = "dummy";
    }
    sink_options.log = log_sink_;
    return std::unique_ptr<AudioSink>(
        std::make_unique<DesktopAudioSink>(std::move(sink_options)));
  }

  absl::StatusOr<std::shared_ptr<ControlTaskRunner>> CreateTaskRunner(
      TaskPriority priority, BlockingPolicy blocking) override {
    const PortCallScope scope;
    static_cast<void>(priority);
    static_cast<void>(blocking);
    if (control_runner_ == nullptr) {
      control_runner_ = std::make_shared<DesktopTaskRunner>();
    }
    return control_runner_;
  }

  absl::StatusOr<std::shared_ptr<HttpTransport>> GetHttpTransport() override {
    const PortCallScope scope;
    return std::static_pointer_cast<HttpTransport>(transport_);
  }

  std::vector<std::shared_ptr<ByteStreamProvider>> ByteStreamProviders()
      const override {
    const PortCallScope scope;
    return {file_provider_, http_provider_};
  }

  std::vector<std::shared_ptr<ElementaryStreamProvider>>
  ElementaryStreamProviders() const override {
    const PortCallScope scope;
    return {};
  }

  absl::StatusOr<FallbackProbe> ProbeFallbackDemuxer(
      std::span<const uint8_t> window,
      std::string_view name_hint) const override {
    const PortCallScope scope;
    return fallback_provider_.ProbeFallbackDemuxer(window, name_hint);
  }

  absl::StatusOr<std::unique_ptr<FallbackDemuxer>> OpenFallbackDemuxer(
      FallbackDemuxerRequest request,
      std::shared_ptr<FallbackByteSource> source,
      std::shared_ptr<FallbackDemuxerCallbacks> callbacks) override {
    const PortCallScope scope;
    return fallback_provider_.OpenFallbackDemuxer(
        std::move(request), std::move(source), std::move(callbacks));
  }

  absl::Status Log(LogLevel level, std::string_view redacted_message) override {
    const PortCallScope scope;
    return log_sink_->Log(level, redacted_message);
  }

  BufferBudgets GetBufferBudgets() const override {
    const PortCallScope scope;
    return options_.buffer_budgets;
  }

  absl::Status SetMemoryPressureObserver(
      std::shared_ptr<MemoryPressureObserver> observer) override {
    const PortCallScope scope;
    memory_pressure_observer_ = std::move(observer);
    return absl::OkStatus();
  }

  absl::StatusOr<std::vector<float>> ConvertAudio(
      const StreamConfig& stream, const DecodedAudioBuffer& buffer) override {
    const auto* format = std::get_if<AudioInfo>(&stream.info.format);
    if (format == nullptr) {
      return absl::InvalidArgumentError(
          "audio conversion needs an audio track");
    }
    auto found = audio_converters_.find(stream.info.track_id);
    if (found == audio_converters_.end()) {
      AudioConversionConfig conversion;
      conversion.output_sample_rate = format->sample_rate;
      conversion.output_channel_count = format->channel_count;
      conversion.output_channel_mask = format->channel_mask;
      conversion.preserves_pitch = audio_preserves_pitch_;
      conversion.minimum_audible_rate = minimum_audible_rate_;
      conversion.maximum_audible_rate = maximum_audible_rate_;
      auto converter = AudioConverter::Create(conversion);
      if (!converter.ok()) {
        return converter.status();
      }
      auto status = (*converter)->SetVolume(static_cast<float>(audio_volume_));
      if (!status.ok()) {
        return status;
      }
      if (audio_rate_ > 0) {
        status = (*converter)->SetTimeStretchRatio(audio_rate_);
        if (!status.ok()) {
          return status;
        }
      }
      found =
          audio_converters_.emplace(stream.info.track_id, std::move(*converter))
              .first;
    }
    auto converted = found->second->Convert(buffer, format->channel_mask);
    if (!converted.ok()) {
      return converted.status();
    }
    return std::move(converted->samples);
  }

  absl::Status SetAudioVolume(double volume) override {
    audio_volume_ = volume;
    for (auto& [track_id, converter] : audio_converters_) {
      static_cast<void>(track_id);
      auto status = converter->SetVolume(static_cast<float>(volume));
      if (!status.ok()) {
        return status;
      }
    }
    return absl::OkStatus();
  }

  absl::Status SetAudioRate(double rate) override {
    audio_rate_ = rate;
    if (rate == 0) {
      return absl::OkStatus();
    }
    for (auto& [track_id, converter] : audio_converters_) {
      static_cast<void>(track_id);
      auto status = converter->SetTimeStretchRatio(rate);
      if (!status.ok()) {
        return status;
      }
    }
    return absl::OkStatus();
  }

  absl::Status SetAudioPreservesPitch(bool preserves_pitch) override {
    audio_preserves_pitch_ = preserves_pitch;
    for (auto& [track_id, converter] : audio_converters_) {
      static_cast<void>(track_id);
      converter->SetPreservesPitch(preserves_pitch);
    }
    return absl::OkStatus();
  }

  absl::Status ConfigureAudioRateRange(double minimum_rate,
                                       double maximum_rate) override {
    if (!std::isfinite(minimum_rate) || !std::isfinite(maximum_rate) ||
        minimum_rate < 0.25 || minimum_rate > maximum_rate ||
        maximum_rate > 4.0) {
      return absl::InvalidArgumentError("audio playback rate bounds invalid");
    }
    minimum_audible_rate_ = minimum_rate;
    maximum_audible_rate_ = maximum_rate;
    return absl::OkStatus();
  }

  void ResetAudioConverters() override {
    for (auto& [track_id, converter] : audio_converters_) {
      static_cast<void>(track_id);
      converter->Reset();
    }
  }

 private:
  class LeasedPlayer final : public Player {
   public:
    LeasedPlayer(std::unique_ptr<Player> player, std::shared_ptr<int> lease)
        : player_(std::move(player)), lease_(std::move(lease)) {}
    LeasedPlayer(const LeasedPlayer&) = delete;
    LeasedPlayer& operator=(const LeasedPlayer&) = delete;
    ~LeasedPlayer() override = default;
    absl::Status WriteSamples(SeekTicket ticket, uint32_t track_id,
                              std::span<const EncodedSample> samples) override {
      return player_->WriteSamples(ticket, track_id, samples);
    }
    absl::Status WriteFragment(SeekTicket ticket, uint32_t requested_track,
                               const MediaFragment& fragment) override {
      return player_->WriteFragment(ticket, requested_track, fragment);
    }
    absl::Status EndOfStream(SeekTicket ticket, uint32_t track_id) override {
      return player_->EndOfStream(ticket, track_id);
    }
    absl::Status Seek(SeekTicket ticket, MediaTime position) override {
      return player_->Seek(ticket, position);
    }
    absl::Status Flush(SeekTicket ticket) override {
      return player_->Flush(ticket);
    }
    absl::Status SetPlaybackRate(double rate) override {
      return player_->SetPlaybackRate(rate);
    }
    absl::Status SetPreservesPitch(bool preserves_pitch) override {
      return player_->SetPreservesPitch(preserves_pitch);
    }
    absl::Status SetVolume(double volume) override {
      return player_->SetVolume(volume);
    }
    absl::Status SetBounds(uint32_t track_id, OutputBounds bounds) override {
      return player_->SetBounds(track_id, bounds);
    }
    absl::StatusOr<PlaybackSnapshot> GetSnapshot() const override {
      return player_->GetSnapshot();
    }
    absl::StatusOr<std::optional<DecodedVideoFrame>> GetCurrentFrame(
        uint32_t track_id) const override {
      return player_->GetCurrentFrame(track_id);
    }
    absl::Status Stop() override { return player_->Stop(); }

   private:
    std::unique_ptr<Player> player_;
    std::shared_ptr<int> lease_;
  };

  DesktopPort(DesktopPortOptions options,
              std::shared_ptr<CurlHttpTransport> transport)
      : options_(std::move(options)),
        transport_(std::move(transport)),
        log_sink_(std::make_shared<StderrLogSink>()),
        file_provider_(std::make_shared<FileByteStreamProvider>()),
        http_provider_(
            std::make_shared<HttpByteStreamProvider>(transport_, options_)),
        capabilities_({.disabled_decoders = options_.disabled_decoders,
                       .max_video_height = options_.max_video_height},
                      std::make_shared<SoftwareDecoderProbe>()),
        components_({.video_threads = options_.video_threads,
                     .av1_apply_film_grain = options_.av1_apply_film_grain,
                     .av1_max_frame_delay = options_.av1_max_frame_delay,
                     .av1_operating_point = options_.av1_operating_point}) {}

  DesktopPortOptions options_;
  SDL_Window* window_ = nullptr;
  std::function<void(GpuPresenter&)> on_presenter_;
  std::shared_ptr<CurlHttpTransport> transport_;
  std::shared_ptr<StderrLogSink> log_sink_;
  std::shared_ptr<FileByteStreamProvider> file_provider_;
  std::shared_ptr<HttpByteStreamProvider> http_provider_;
  DesktopCapabilityProvider capabilities_;
  DesktopComponentFactory components_;
  FfmpegFallbackProvider fallback_provider_;
  std::shared_ptr<MemoryPressureObserver> memory_pressure_observer_;
  std::shared_ptr<ControlTaskRunner> control_runner_;
  std::weak_ptr<int> active_player_;
  std::unordered_map<uint32_t, std::unique_ptr<AudioConverter>>
      audio_converters_;
  double audio_volume_ = 1.0;
  double audio_rate_ = 1.0;
  bool audio_preserves_pitch_ = true;
  double minimum_audible_rate_ = 0.25;
  double maximum_audible_rate_ = 4.0;
};

}

absl::StatusOr<std::unique_ptr<Port>> CreatePort(DesktopPortOptions options) {
  auto port = DesktopPort::Create(std::move(options));
  if (!port.ok()) {
    return port.status();
  }
  return std::unique_ptr<Port>(std::move(*port));
}

DesktopWindowControl* GetDesktopWindowControl(Port& port) {
  return dynamic_cast<DesktopWindowControl*>(&port);
}

}

namespace penumbra {

absl::StatusOr<std::unique_ptr<Port>> CreateDefaultPort(bool headless) {
  desktop::DesktopPortOptions options;
  options.headless = headless;
  {
    auto& factories = desktop::DefaultSinkFactoriesState();
    const std::scoped_lock lock(factories.mutex);
    options.audio_sink_factory = factories.audio;
    options.video_sink_factory = factories.video;
  }
  return desktop::CreatePort(std::move(options));
}

absl::Status desktop::SetControlTaskRunnerForTesting(
    Port& port, std::shared_ptr<ControlTaskRunner> control) {
  auto* desktop_port = dynamic_cast<desktop::DesktopPort*>(&port);
  if (desktop_port == nullptr || control == nullptr) {
    return absl::InvalidArgumentError("invalid desktop control runner");
  }
  desktop_port->SetControlRunner(std::move(control));
  return absl::OkStatus();
}

void desktop::SetDefaultAudioSinkFactoryForTesting(
    std::function<absl::StatusOr<std::unique_ptr<AudioSink>>()> factory) {
  auto& factories = desktop::DefaultSinkFactoriesState();
  const std::scoped_lock lock(factories.mutex);
  factories.audio = std::move(factory);
}

void desktop::SetDefaultVideoSinkFactoryForTesting(
    std::function<absl::StatusOr<std::unique_ptr<VideoSink>>(OutputMode)>
        factory) {
  auto& factories = desktop::DefaultSinkFactoriesState();
  const std::scoped_lock lock(factories.mutex);
  factories.video = std::move(factory);
}

}
