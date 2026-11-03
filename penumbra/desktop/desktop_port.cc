#include "penumbra/desktop/desktop_port.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/time/time.h"
#include "penumbra/desktop/audio/audio_sink.h"
#include "penumbra/desktop/capability_provider.h"
#include "penumbra/desktop/curl_http_transport.h"
#include "penumbra/desktop/desktop_component_factory.h"
#include "penumbra/desktop/ffmpeg_fallback_demuxer.h"
#include "penumbra/desktop/file_byte_stream_provider.h"
#include "penumbra/desktop/software_decoder_probe.h"
#include "penumbra/desktop/stderr_log_sink.h"
#include "penumbra/desktop/video/gpu_presenter.h"
#include "penumbra/desktop/video/presenter.h"
#include "penumbra/desktop/video/sdl_session.h"
#include "penumbra/desktop/video/video_geometry.h"
#include "penumbra/desktop/video/video_sink.h"
#include "penumbra/interface/callback_dispatcher.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/decoder_types.h"
#include "penumbra/interface/default_port.h"
#include "penumbra/interface/fallback_demuxer.h"
#include "penumbra/interface/http_transport.h"
#include "penumbra/interface/interface.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/stream_info.h"
#include "penumbra/interface/stream_provider.h"
#include "penumbra/interface/task_runner.h"

namespace penumbra::desktop {
namespace {

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
  std::thread worker_;
  std::thread::id worker_id_;
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

class DesktopPort final : public Port {
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

  absl::StatusOr<PortLimits> GetLimits() const override {
    const PortCallScope scope;
    return PortLimits{.api_version = kApiVersion, .max_players = 1};
  }

  absl::StatusOr<std::unique_ptr<Player>> CreatePlayer(
      PlayerConfig config,
      std::shared_ptr<PlayerCallbacks> callbacks) override {
    const PortCallScope scope;
    config.decoder = options_.decoder_settings;
    static_cast<void>(config);
    static_cast<void>(callbacks);
    return absl::UnimplementedError(
        "the reference player implementation is not available");
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
    if (mode != OutputMode::kTexture) {
      return absl::UnimplementedError(
          "the desktop port supports texture video output only");
    }
    if (video_session_ == nullptr) {
      const SessionMode session_mode =
          options_.headless ? SessionMode::kHeadless : SessionMode::kWindowed;
      auto session = SdlVideoSession::Create(session_mode);
      if (!session.ok()) {
        return session.status();
      }
      video_session_ = std::move(*session);
    }
    if (!options_.headless && window_ == nullptr) {
      auto window =
          SdlWindow::Create(*video_session_, options_.window_title,
                            PixelSize{.width = options_.window_width,
                                      .height = options_.window_height});
      if (!window.ok()) {
        return window.status();
      }
      window_ = std::move(*window);
    }
    const GpuPresenterOptions presenter_options{
        .window = window_ != nullptr ? window_->get() : nullptr,
        .offscreen_logical_size = {.width = 640, .height = 360},
        .offscreen_pixel_density = 1.0F,
        .scaling_filter = ScalingFilter::kLinear,
        .frames_in_flight = 1,
        .debug_mode = false};
    auto presenter = GpuPresenter::Create(presenter_options);
    if (!presenter.ok()) {
      return presenter.status();
    }
    std::shared_ptr<Presenter> shared_presenter(std::move(*presenter));
    return std::unique_ptr<VideoSink>(
        std::make_unique<DesktopVideoSink>(std::move(shared_presenter)));
  }

  absl::StatusOr<std::unique_ptr<AudioSink>> CreateAudioSink() override {
    const PortCallScope scope;
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
    return std::shared_ptr<ControlTaskRunner>(
        std::make_shared<DesktopTaskRunner>());
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

 private:
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
  std::unique_ptr<SdlVideoSession> video_session_;
  std::unique_ptr<SdlWindow> window_;
  std::shared_ptr<CurlHttpTransport> transport_;
  std::shared_ptr<StderrLogSink> log_sink_;
  std::shared_ptr<FileByteStreamProvider> file_provider_;
  std::shared_ptr<HttpByteStreamProvider> http_provider_;
  DesktopCapabilityProvider capabilities_;
  DesktopComponentFactory components_;
  FfmpegFallbackProvider fallback_provider_;
  std::shared_ptr<MemoryPressureObserver> memory_pressure_observer_;
};

}

absl::StatusOr<std::unique_ptr<Port>> CreatePort(DesktopPortOptions options) {
  auto port = DesktopPort::Create(std::move(options));
  if (!port.ok()) {
    return port.status();
  }
  return std::unique_ptr<Port>(std::move(*port));
}

}

namespace penumbra {

absl::StatusOr<std::unique_ptr<Port>> CreateDefaultPort(bool headless) {
  desktop::DesktopPortOptions options;
  options.headless = headless;
  return desktop::CreatePort(std::move(options));
}

}
