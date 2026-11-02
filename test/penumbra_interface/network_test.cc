#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "fake_network.h"
#include "fake_port.h"
#include "gtest/gtest.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/http_transport.h"
#include "penumbra/interface/port.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"
#include "penumbra/interface/stream_provider.h"
#include "penumbra/interface/task_runner.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/status.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/test_support/test_task_environment.h"
#include "umbra/base/url_redactor.h"

namespace penumbra::test {
namespace {

static_assert(!std::is_abstract_v<FakeHttpTransport>);
static_assert(!std::is_abstract_v<FakeByteStreamProvider>);
static_assert(!std::is_abstract_v<FakeElementaryStreamProvider>);
static_assert(std::is_base_of_v<HttpTransportProvider, Port>);
static_assert(std::is_base_of_v<StreamProviderCatalogue, Port>);

struct HttpEvent {
  enum class Kind { kResponse, kChunk, kComplete, kError };
  Kind kind = Kind::kResponse;
  std::optional<HttpResponseHead> head = std::nullopt;
  std::optional<HttpChunk> chunk = std::nullopt;
  std::optional<HttpMetrics> metrics = std::nullopt;
  absl::Status error = absl::OkStatus();
};

class HttpRecorder : public HttpTransportCallbacks {
 public:
  absl::Status OnResponse(HttpResponseHead head) override {
    Add({.kind = HttpEvent::Kind::kResponse, .head = std::move(head)});
    return result;
  }
  absl::Status OnChunk(HttpChunk chunk) override {
    Add({.kind = HttpEvent::Kind::kChunk, .chunk = std::move(chunk)});
    return result;
  }
  absl::Status OnComplete(HttpMetrics metrics) override {
    Add({.kind = HttpEvent::Kind::kComplete, .metrics = std::move(metrics)});
    return result;
  }
  absl::Status OnError(absl::Status error) override {
    Add({.kind = HttpEvent::Kind::kError, .error = std::move(error)});
    return result;
  }
  std::vector<HttpEvent> Events() const {
    std::scoped_lock const lock(mutex);
    return events;
  }
  std::vector<HttpEvent::Kind> Kinds() const {
    std::vector<HttpEvent::Kind> kinds;
    for (const auto& event : Events()) {
      kinds.push_back(event.kind);
    }
    return kinds;
  }
  absl::Status result;

 protected:
  void Add(HttpEvent event) {
    std::scoped_lock const lock(mutex);
    events.push_back(std::move(event));
  }
  mutable std::mutex mutex;
  std::vector<HttpEvent> events;
};

using Kind = HttpEvent::Kind;

HttpChunk Chunk(std::vector<uint8_t> bytes, int64_t arrival_ms) {
  return {.data = SharedBytes::Adopt(std::move(bytes)),
          .arrival_time = absl::Milliseconds(arrival_ms)};
}

HttpScript TwoChunkScript() {
  HttpScript script;
  script.headers = {{"Content-Type", "video/mp4"}, {"Content-Length", "6"}};
  script.chunks = {Chunk({1, 2, 3, 4}, 20), Chunk({5, 6}, 45)};
  script.request_start = absl::Milliseconds(5);
  script.response_start = absl::Milliseconds(12);
  script.completion = absl::Milliseconds(46);
  script.final_url = "https://cdn.test/final.mp4";
  return script;
}

HttpRequest Get(std::string url = "https://origin.test/media.mp4") {
  HttpRequest request;
  request.url = std::move(url);
  return request;
}

class NetworkTest : public ::testing::Test {
 protected:
  NetworkTest()
      : control_(std::make_shared<RunnerAdapter>(
            environment_.CreateSequencedTaskRunner(
                umbra::base::TaskPriority::kControl))),
        port_(control_),
        recorder_(std::make_shared<HttpRecorder>()) {}

  absl::StatusOr<std::unique_ptr<HttpRequestHandle>> Start(
      HttpRequest request = Get()) {
    return port_.Http().Start(std::move(request), recorder_);
  }

  umbra::base::TestTaskEnvironment environment_;
  std::shared_ptr<RunnerAdapter> control_;
  FakePort port_;
  std::shared_ptr<HttpRecorder> recorder_;
};

TEST_F(NetworkTest, PortHandsOutOneTransport) {
  auto first = port_.GetHttpTransport();
  auto second = port_.GetHttpTransport();
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(second.ok());
  EXPECT_EQ(first->get(), second->get());
}

TEST_F(NetworkTest, DeliversHeadChunksAndMetricsInOrderWithoutCopying) {
  auto script = TwoChunkScript();
  const uint8_t* first_bytes = script.chunks[0].data.data();
  ASSERT_TRUE(port_.Http().SetScript(script).ok());
  auto handle = Start();
  ASSERT_TRUE(handle.ok());
  EXPECT_EQ(port_.Http().Pump(), 4U);
  EXPECT_EQ(recorder_->Kinds(),
            (std::vector<Kind>{Kind::kResponse, Kind::kChunk, Kind::kChunk,
                               Kind::kComplete}));
  const auto events = recorder_->Events();
  EXPECT_EQ(events[0].head->status, 200);
  EXPECT_EQ(events[0].head->protocol, HttpProtocol::kHttp2);
  EXPECT_EQ(events[0].head->final_url, "https://cdn.test/final.mp4");
  EXPECT_EQ(events[0].head->headers.size(), 2U);
  EXPECT_EQ(events[1].chunk->data.data(), first_bytes);
  EXPECT_EQ(events[1].chunk->arrival_time, absl::Milliseconds(20));
  EXPECT_EQ(events[2].chunk->arrival_time, absl::Milliseconds(45));
  EXPECT_LE(events[1].chunk->arrival_time, events[2].chunk->arrival_time);
  const auto& metrics = *events[3].metrics;
  EXPECT_EQ(metrics.status, 200);
  EXPECT_EQ(metrics.body_bytes, 6U);
  EXPECT_EQ(metrics.final_url, "https://cdn.test/final.mp4");
  EXPECT_EQ(metrics.request_start, absl::Milliseconds(5));
  EXPECT_EQ(metrics.response_start, absl::Milliseconds(12));
  EXPECT_EQ(metrics.completion, absl::Milliseconds(46));
  EXPECT_FALSE(metrics.dns.has_value());
  EXPECT_FALSE(metrics.connect.has_value());
  EXPECT_FALSE(metrics.tls.has_value());
  EXPECT_FALSE(metrics.coarse_timing);
}

TEST_F(NetworkTest, FinalUrlDefaultsToTheRequestUrl) {
  HttpScript script;
  script.chunks = {Chunk({1}, 1)};
  script.completion = absl::Milliseconds(1);
  ASSERT_TRUE(port_.Http().SetScript(std::move(script)).ok());
  auto handle = Start(Get("https://origin.test/a"));
  ASSERT_TRUE(handle.ok());
  port_.Http().Pump();
  EXPECT_EQ(recorder_->Events().front().head->final_url,
            "https://origin.test/a");
}

TEST_F(NetworkTest, CoarseTimingIsReportedInTheMetrics) {
  auto script = TwoChunkScript();
  script.coarse_timing = true;
  ASSERT_TRUE(port_.Http().SetScript(std::move(script)).ok());
  auto handle = Start();
  ASSERT_TRUE(handle.ok());
  port_.Http().Pump();
  EXPECT_TRUE(recorder_->Events().back().metrics->coarse_timing);
}

TEST_F(NetworkTest, AnHttpErrorStatusIsAResponseNotATransportError) {
  HttpScript script;
  script.status = 503;
  script.chunks = {Chunk({'n', 'o'}, 3)};
  script.completion = absl::Milliseconds(3);
  ASSERT_TRUE(port_.Http().SetScript(std::move(script)).ok());
  auto handle = Start();
  ASSERT_TRUE(handle.ok());
  port_.Http().Pump();
  EXPECT_EQ(
      recorder_->Kinds(),
      (std::vector<Kind>{Kind::kResponse, Kind::kChunk, Kind::kComplete}));
  EXPECT_EQ(recorder_->Events().front().head->status, 503);
  EXPECT_EQ(recorder_->Events().back().metrics->status, 503);
}

TEST_F(NetworkTest, TransportFailureKeepsItsCanonicalStatusAndErrorDetail) {
  const umbra::base::UrlRedactor redactor({1, 2});
  HttpScript script;
  script.status = 0;
  script.failure =
      umbra::base::MakeError(umbra::base::ErrorCode::kNetworkTimeout, redactor,
                             {{"cause", "connect"}});
  ASSERT_TRUE(port_.Http().SetScript(std::move(script)).ok());
  auto handle = Start();
  ASSERT_TRUE(handle.ok());
  port_.Http().Pump();
  EXPECT_EQ(recorder_->Kinds(), (std::vector<Kind>{Kind::kError}));
  const absl::Status error = recorder_->Events().front().error;
  EXPECT_EQ(error.code(), absl::StatusCode::kUnavailable);
  const auto detail = umbra::base::GetErrorDetail(error);
  ASSERT_TRUE(detail.ok());
  ASSERT_TRUE(detail->has_value());
  EXPECT_EQ((*detail)->code, umbra::base::ErrorCode::kNetworkTimeout);
  EXPECT_EQ((*detail)->context.at("cause"), "connect");
}

TEST_F(NetworkTest, FailureAfterPartialBodyDeliversTheChunksFirst) {
  HttpScript script;
  script.chunks = {Chunk({1, 2}, 4)};
  script.completion = absl::Milliseconds(5);
  script.failure = absl::UnavailableError("connection reset");
  ASSERT_TRUE(port_.Http().SetScript(std::move(script)).ok());
  auto handle = Start();
  ASSERT_TRUE(handle.ok());
  port_.Http().Pump();
  EXPECT_EQ(recorder_->Kinds(),
            (std::vector<Kind>{Kind::kResponse, Kind::kChunk, Kind::kError}));
}

TEST_F(NetworkTest, CancelIsIdempotentAndNothingIsDeliveredAfterIt) {
  ASSERT_TRUE(port_.Http().SetScript(TwoChunkScript()).ok());
  auto handle = Start();
  ASSERT_TRUE(handle.ok());
  EXPECT_EQ(port_.Http().Pump(2), 2U);
  (*handle)->Cancel();
  (*handle)->Cancel();
  EXPECT_EQ(port_.Http().Pump(), 0U);
  EXPECT_EQ(recorder_->Kinds(),
            (std::vector<Kind>{Kind::kResponse, Kind::kChunk}));
  EXPECT_EQ((*handle)->Pause().code(), absl::StatusCode::kFailedPrecondition);
}

TEST_F(NetworkTest, DestroyingTheHandleCancelsTheRequest) {
  ASSERT_TRUE(port_.Http().SetScript(TwoChunkScript()).ok());
  auto handle = Start();
  ASSERT_TRUE(handle.ok());
  EXPECT_EQ(port_.Http().Pump(1), 1U);
  handle->reset();
  EXPECT_EQ(port_.Http().Pump(), 0U);
  EXPECT_EQ(recorder_->Kinds(), (std::vector<Kind>{Kind::kResponse}));
}

TEST_F(NetworkTest, PauseHoldsTheBodyBackUntilResume) {
  ASSERT_TRUE(port_.Http().SetScript(TwoChunkScript()).ok());
  auto handle = Start();
  ASSERT_TRUE(handle.ok());
  EXPECT_EQ(port_.Http().Pump(2), 2U);
  EXPECT_TRUE((*handle)->Pause().ok());
  EXPECT_EQ(port_.Http().Pump(), 0U);
  EXPECT_EQ(recorder_->Kinds().size(), 2U);
  EXPECT_TRUE((*handle)->Resume().ok());
  EXPECT_EQ(port_.Http().Pump(), 2U);
  EXPECT_EQ(recorder_->Kinds().back(), Kind::kComplete);
  EXPECT_EQ((*handle)->Pause().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ((*handle)->Resume().code(), absl::StatusCode::kFailedPrecondition);
}

TEST_F(NetworkTest, StartRejectsInvalidRequestsWithoutAnyCallback) {
  auto request = Get();
  request.body = SharedBytes::Adopt({1});
  EXPECT_EQ(Start(request).status().code(), absl::StatusCode::kInvalidArgument);
  request = Get("");
  EXPECT_EQ(Start(request).status().code(), absl::StatusCode::kInvalidArgument);
  request = Get();
  request.connect_timeout = absl::ZeroDuration();
  EXPECT_EQ(Start(request).status().code(), absl::StatusCode::kInvalidArgument);
  request = Get();
  request.redirects.max_redirects = -1;
  EXPECT_EQ(Start(request).status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(port_.Http().Start(Get(), nullptr).status().code(),
            absl::StatusCode::kInvalidArgument);
  port_.Http().Pump();
  EXPECT_TRUE(recorder_->Events().empty());
  EXPECT_TRUE(port_.Http().Requests().empty());
}

TEST_F(NetworkTest, RequestsCarryTheirClassPriorityRangeBodyAndOptIns) {
  ASSERT_TRUE(port_.Http().SetScript(TwoChunkScript()).ok());
  const std::array classes = {RequestClass::kManifest, RequestClass::kLicence,
                              RequestClass::kInit, RequestClass::kMedia,
                              RequestClass::kPrefetch};
  std::vector<std::unique_ptr<HttpRequestHandle>> handles;
  for (const RequestClass request_class : classes) {
    auto request = Get();
    request.request_class = request_class;
    request.priority = RequestPriority::kHigh;
    request.range = ByteRange{.offset = 100, .length = 50};
    request.include_credentials = true;
    request.redirects = {.max_redirects = 3, .allow_https_downgrade = false};
    if (request_class == RequestClass::kLicence) {
      request.method = HttpMethod::kPost;
      request.body = SharedBytes::Adopt({9, 9});
    }
    auto handle = Start(std::move(request));
    ASSERT_TRUE(handle.ok());
    handles.push_back(*std::move(handle));
  }
  const auto requests = port_.Http().Requests();
  ASSERT_EQ(requests.size(), classes.size());
  for (size_t i = 0; i < classes.size(); ++i) {
    EXPECT_EQ(requests[i].request_class, classes.at(i));
    EXPECT_EQ(requests[i].priority, RequestPriority::kHigh);
    EXPECT_EQ(requests[i].range->offset, 100U);
    EXPECT_EQ(requests[i].range->length, 50U);
    EXPECT_TRUE(requests[i].include_credentials);
    EXPECT_EQ(requests[i].redirects.max_redirects, 3);
  }
  EXPECT_EQ(requests[1].body.size(), 2U);
}

TEST_F(NetworkTest, TheFakeRejectsScriptsThatBreakTheTimingContract) {
  HttpScript script = TwoChunkScript();
  script.chunks[1].arrival_time = absl::Milliseconds(10);
  EXPECT_EQ(port_.Http().SetScript(script).code(),
            absl::StatusCode::kInvalidArgument);
  script = TwoChunkScript();
  script.completion = absl::Milliseconds(30);
  EXPECT_EQ(port_.Http().SetScript(script).code(),
            absl::StatusCode::kInvalidArgument);
  script = TwoChunkScript();
  script.status = 42;
  EXPECT_EQ(port_.Http().SetScript(script).code(),
            absl::StatusCode::kInvalidArgument);
  script = TwoChunkScript();
  script.chunks[0].data = SharedBytes();
  EXPECT_EQ(port_.Http().SetScript(script).code(),
            absl::StatusCode::kInvalidArgument);
}

class BlockingRecorder final : public HttpRecorder {
 public:
  absl::Status OnResponse(HttpResponseHead head) override {
    entered = true;
    while (!release) {
      std::this_thread::yield();
    }
    return HttpRecorder::OnResponse(std::move(head));
  }
  std::atomic<bool> entered{false};
  std::atomic<bool> release{false};
};

TEST_F(NetworkTest, StopIsTerminalNonblockingAndObservesInFlightCallbacks) {
  ASSERT_TRUE(port_.Http().SetScript(TwoChunkScript()).ok());
  auto blocker = std::make_shared<BlockingRecorder>();
  auto handle = port_.Http().Start(Get(), blocker);
  ASSERT_TRUE(handle.ok());
  std::thread platform([this] { port_.Http().Pump(); });
  while (!blocker->entered) {
    std::this_thread::yield();
  }
  EXPECT_EQ(port_.Http().Stop().code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(Start().status().code(), absl::StatusCode::kFailedPrecondition);
  blocker->release = true;
  platform.join();
  EXPECT_TRUE(port_.Http().Stop().ok());
  EXPECT_TRUE(port_.Http().Stop().ok());
  EXPECT_EQ(port_.Http().Pump(), 0U);
  EXPECT_EQ(Start().status().code(), absl::StatusCode::kFailedPrecondition);
}

class MarshallingRecorder final : public HttpTransportCallbacks {
 public:
  explicit MarshallingRecorder(std::shared_ptr<ControlTaskRunner> control)
      : control_(std::move(control)) {}

  absl::Status OnResponse(HttpResponseHead head) override {
    return Post(Kind::kResponse, head.status);
  }
  absl::Status OnChunk(HttpChunk chunk) override {
    return Post(Kind::kChunk, static_cast<int>(chunk.data.size()));
  }
  absl::Status OnComplete(HttpMetrics metrics) override {
    return Post(Kind::kComplete, static_cast<int>(metrics.body_bytes));
  }
  absl::Status OnError(absl::Status error) override {
    return Post(Kind::kError, static_cast<int>(error.code()));
  }
  std::vector<std::pair<Kind, int>> delivered;
  std::atomic<int> off_sequence{0};

 private:
  absl::Status Post(Kind kind, int value) {
    return control_->PostTask(
        [this, kind, value]() {
          if (!control_->RunsTasksInCurrentSequence()) {
            ++off_sequence;
          }
          delivered.emplace_back(kind, value);
        },
        absl::ZeroDuration());
  }
  std::shared_ptr<ControlTaskRunner> control_;
};

TEST_F(NetworkTest, CallbacksFromAPlatformThreadMarshalOntoTheNetworkSequence) {
  ASSERT_TRUE(port_.Http().SetScript(TwoChunkScript()).ok());
  auto receiver = std::make_shared<MarshallingRecorder>(control_);
  auto handle = port_.Http().Start(Get(), receiver);
  ASSERT_TRUE(handle.ok());
  std::thread platform([this] { port_.Http().Pump(); });
  platform.join();
  environment_.RunUntilIdle();
  EXPECT_EQ(receiver->off_sequence.load(), 0);
  using Delivered = std::vector<std::pair<Kind, int>>;
  EXPECT_EQ(receiver->delivered, (Delivered{{Kind::kResponse, 200},
                                            {Kind::kChunk, 4},
                                            {Kind::kChunk, 2},
                                            {Kind::kComplete, 6}}));
}

TEST_F(NetworkTest, FailedCallbackDeliveryIsObservable) {
  ASSERT_TRUE(port_.Http().SetScript(TwoChunkScript()).ok());
  recorder_->result = absl::UnavailableError("receiver stopped");
  auto handle = Start();
  ASSERT_TRUE(handle.ok());
  EXPECT_EQ(port_.Http().Pump(), 4U);
}

struct ByteEvent {
  enum class Kind { kOpen, kData, kEnd, kError };
  Kind kind = Kind::kOpen;
  std::optional<ByteStreamInfo> info = std::nullopt;
  std::optional<ByteStreamChunk> chunk = std::nullopt;
  absl::Status error = absl::OkStatus();
};

class ByteRecorder final : public ByteStreamCallbacks {
 public:
  absl::Status OnOpen(ByteStreamInfo info) override {
    events.push_back({.kind = ByteEvent::Kind::kOpen, .info = std::move(info)});
    return absl::OkStatus();
  }
  absl::Status OnData(ByteStreamChunk chunk) override {
    events.push_back(
        {.kind = ByteEvent::Kind::kData, .chunk = std::move(chunk)});
    return absl::OkStatus();
  }
  absl::Status OnEnd() override {
    events.push_back({.kind = ByteEvent::Kind::kEnd});
    return absl::OkStatus();
  }
  absl::Status OnError(absl::Status error) override {
    events.push_back(
        {.kind = ByteEvent::Kind::kError, .error = std::move(error)});
    return absl::OkStatus();
  }
  std::vector<ByteEvent> events;
};

TEST_F(NetworkTest, PortRegistersItsStreamProvidersThroughTheCatalogue) {
  const auto bytes = port_.ByteStreamProviders();
  ASSERT_EQ(bytes.size(), 1U);
  EXPECT_EQ(bytes[0]->Schemes(), (std::vector<std::string>{"fake", "fakes"}));
  const auto elementary = port_.ElementaryStreamProviders();
  ASSERT_EQ(elementary.size(), 1U);
  EXPECT_EQ(elementary[0]->Schemes(), (std::vector<std::string>{"fakeelem"}));
}

TEST_F(NetworkTest, ByteStreamReportsSeekabilityAndSizeThenDeliversBytes) {
  ByteStreamScript script;
  script.info = {.seekable = true,
                 .size = 6,
                 .content_type = "video/mp2t",
                 .final_url = "fake://host/final"};
  script.chunks = {{.data = SharedBytes::Adopt({1, 2, 3}),
                    .arrival_time = absl::Milliseconds(1)},
                   {.data = SharedBytes::Adopt({4, 5, 6}),
                    .arrival_time = absl::Milliseconds(2)}};
  port_.ByteStreams().SetScript(script);
  auto recorder = std::make_shared<ByteRecorder>();
  auto stream = port_.ByteStreams().Open(
      {.url = "FAKE://host/media",
       .offset = 3,
       .length = 3,
       .credential = Credential{.username = "u", .password = "p"},
       .request_class = RequestClass::kInit},
      recorder);
  ASSERT_TRUE(stream.ok());
  EXPECT_EQ(port_.ByteStreams().Pump(), 4U);
  ASSERT_EQ(recorder->events.size(), 4U);
  EXPECT_EQ(recorder->events[0].kind, ByteEvent::Kind::kOpen);
  EXPECT_TRUE(recorder->events[0].info->seekable);
  EXPECT_EQ(recorder->events[0].info->size, 6U);
  EXPECT_EQ(recorder->events[1].chunk->data.size(), 3U);
  EXPECT_EQ(recorder->events[3].kind, ByteEvent::Kind::kEnd);
  const auto requests = port_.ByteStreams().Requests();
  ASSERT_EQ(requests.size(), 1U);
  EXPECT_EQ(requests[0].offset, 3U);
  EXPECT_EQ(requests[0].length, 3U);
  EXPECT_EQ(requests[0].credential->username, "u");
  EXPECT_EQ(requests[0].request_class, RequestClass::kInit);
}

TEST_F(NetworkTest, LiveByteStreamsAreNeitherSeekableNorSized) {
  port_.ByteStreams().SetScript({});
  auto recorder = std::make_shared<ByteRecorder>();
  EXPECT_EQ(port_.ByteStreams()
                .Open({.url = "fake://host/live", .offset = 10}, recorder)
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);
  auto stream = port_.ByteStreams().Open({.url = "fake://host/live"}, recorder);
  ASSERT_TRUE(stream.ok());
  port_.ByteStreams().Pump();
  EXPECT_FALSE(recorder->events.front().info->seekable);
  EXPECT_FALSE(recorder->events.front().info->size.has_value());
}

TEST_F(NetworkTest, ByteStreamCancelPauseAndFailureFollowTheTransportRules) {
  ByteStreamScript script;
  script.chunks = {
      {.data = SharedBytes::Adopt({1}), .arrival_time = absl::Milliseconds(1)},
      {.data = SharedBytes::Adopt({2}), .arrival_time = absl::Milliseconds(2)}};
  script.failure = absl::DataLossError("truncated");
  port_.ByteStreams().SetScript(script);
  auto recorder = std::make_shared<ByteRecorder>();
  auto stream =
      port_.ByteStreams().Open({.url = "fake://host/media"}, recorder);
  ASSERT_TRUE(stream.ok());
  EXPECT_EQ(port_.ByteStreams().Pump(2), 2U);
  EXPECT_TRUE((*stream)->Pause().ok());
  EXPECT_EQ(port_.ByteStreams().Pump(), 0U);
  EXPECT_TRUE((*stream)->Resume().ok());
  EXPECT_EQ(port_.ByteStreams().Pump(1), 1U);
  (*stream)->Cancel();
  (*stream)->Cancel();
  EXPECT_EQ(port_.ByteStreams().Pump(), 0U);
  EXPECT_EQ(recorder->events.size(), 3U);

  auto failing =
      port_.ByteStreams().Open({.url = "fake://host/media"}, recorder);
  ASSERT_TRUE(failing.ok());
  port_.ByteStreams().Pump();
  EXPECT_EQ(recorder->events.back().kind, ByteEvent::Kind::kError);
  EXPECT_EQ(recorder->events.back().error.code(), absl::StatusCode::kDataLoss);
}

TEST_F(NetworkTest, ByteStreamRejectsForeignSchemesAndMissingCallbacks) {
  auto recorder = std::make_shared<ByteRecorder>();
  EXPECT_EQ(port_.ByteStreams()
                .Open({.url = "rtmp://host/app"}, recorder)
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(port_.ByteStreams()
                .Open({.url = "fake://host/app"}, nullptr)
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);
}

class ElementaryRecorder final : public ElementaryStreamCallbacks {
 public:
  absl::Status OnStreams(std::vector<StreamInfo> value) override {
    streams = std::move(value);
    order.push_back('S');
    return absl::OkStatus();
  }
  absl::Status OnSample(EncodedSample sample,
                        absl::Duration arrival_time) override {
    samples.push_back(std::move(sample));
    arrivals.push_back(arrival_time);
    order.push_back('M');
    return absl::OkStatus();
  }
  absl::Status OnEnd() override {
    order.push_back('E');
    return absl::OkStatus();
  }
  absl::Status OnError(absl::Status error) override {
    failure = std::move(error);
    order.push_back('X');
    return absl::OkStatus();
  }
  std::vector<StreamInfo> streams;
  std::vector<EncodedSample> samples;
  std::vector<absl::Duration> arrivals;
  absl::Status failure;
  std::string order;
};

TEST_F(NetworkTest,
       ElementaryStreamEmitsConfigurationsThenSamplesWithoutCopying) {
  ElementaryStreamScript script;
  StreamInfo info;
  info.track_id = 1;
  info.codec = CodecId::kH264;
  info.time_base = Rational::Of(1, 90000);
  info.format = VideoInfo{};
  script.streams = {info};
  EncodedSample sample;
  sample.track_id = 1;
  sample.data = SharedBytes::Adopt({1, 2, 3});
  script.samples = {sample, sample};
  const uint8_t* bytes = sample.data.data();
  port_.ElementaryStreams().SetScript(script);
  auto recorder = std::make_shared<ElementaryRecorder>();
  auto stream = port_.ElementaryStreams().Open(
      {.url = "fakeelem://cam/live",
       .credential = Credential{.username = "admin", .password = "pw"}},
      recorder);
  ASSERT_TRUE(stream.ok());
  EXPECT_EQ(port_.ElementaryStreams().Pump(), 4U);
  EXPECT_EQ(recorder->order, "SMME");
  ASSERT_EQ(recorder->streams.size(), 1U);
  EXPECT_EQ(recorder->streams[0].codec, CodecId::kH264);
  EXPECT_EQ(recorder->samples[0].data.data(), bytes);
  EXPECT_LT(recorder->arrivals[0], recorder->arrivals[1]);
  EXPECT_EQ(port_.ElementaryStreams().Requests()[0].credential->password, "pw");
}

TEST_F(NetworkTest, ElementaryStreamFailureAndCancelFollowTheTransportRules) {
  ElementaryStreamScript script;
  script.failure = absl::UnavailableError("camera offline");
  port_.ElementaryStreams().SetScript(script);
  auto recorder = std::make_shared<ElementaryRecorder>();
  auto stream =
      port_.ElementaryStreams().Open({.url = "fakeelem://cam/x"}, recorder);
  ASSERT_TRUE(stream.ok());
  EXPECT_EQ(port_.ElementaryStreams().Pump(1), 1U);
  (*stream)->Cancel();
  EXPECT_EQ(port_.ElementaryStreams().Pump(), 0U);
  EXPECT_EQ(recorder->order, "S");
  auto second =
      port_.ElementaryStreams().Open({.url = "fakeelem://cam/y"}, recorder);
  ASSERT_TRUE(second.ok());
  port_.ElementaryStreams().Pump();
  EXPECT_EQ(recorder->order, "SSX");
  EXPECT_EQ(recorder->failure.code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(port_.ElementaryStreams()
                .Open({.url = "rtsp://cam/x"}, recorder)
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);
}

}
}
