#include "umbra/ads/beacon_dispatcher.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "penumbra/interface/http_transport.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/test_support/test_task_environment.h"

namespace umbra::ads {
namespace {

struct RequestState {
  penumbra::HttpRequest request;
  bool cancelled = false;
};

class ManualHandle final : public penumbra::HttpRequestHandle {
 public:
  explicit ManualHandle(std::shared_ptr<RequestState> state)
      : state_(std::move(state)) {}
  void Cancel() override { state_->cancelled = true; }
  absl::Status Pause() override { return absl::OkStatus(); }
  absl::Status Resume() override { return absl::OkStatus(); }

 private:
  std::shared_ptr<RequestState> state_;
};

class ManualTransport final : public penumbra::HttpTransport {
 public:
  absl::StatusOr<std::unique_ptr<penumbra::HttpRequestHandle>> Start(
      penumbra::HttpRequest request,
      std::shared_ptr<penumbra::HttpTransportCallbacks> callbacks) override {
    state = std::make_shared<RequestState>();
    state->request = std::move(request);
    callbacks_ = std::move(callbacks);
    return std::unique_ptr<penumbra::HttpRequestHandle>(
        new ManualHandle(state));
  }
  absl::Status Stop() override { return absl::OkStatus(); }

  std::shared_ptr<RequestState> state;

 private:
  std::shared_ptr<penumbra::HttpTransportCallbacks> callbacks_;
};

TEST(BeaconDispatcherTest, SchedulesLowPriorityGetAndTimesOutWithoutBlocking) {
  umbra::base::TestTaskEnvironment environment;
  auto runner = environment.CreateSequencedTaskRunner(
      umbra::base::TaskPriority::kBackground);
  auto transport = std::make_shared<ManualTransport>();
  bool logged = false;
  bool log_success = true;
  BeaconDispatcher dispatcher(transport, runner, absl::Milliseconds(500),
                              [&logged, &log_success](bool success, int) {
                                logged = true;
                                log_success = success;
                              });
  dispatcher.Dispatch("https://ads.test/beacon?token=secret");
  const bool playback_progressed = true;
  EXPECT_TRUE(playback_progressed);
  EXPECT_FALSE(transport->state);
  environment.RunUntilIdle();
  ASSERT_TRUE(transport->state);
  EXPECT_EQ(transport->state->request.method, penumbra::HttpMethod::kGet);
  EXPECT_EQ(transport->state->request.request_class,
            penumbra::RequestClass::kPrefetch);
  EXPECT_EQ(transport->state->request.priority,
            penumbra::RequestPriority::kLow);
  environment.FastForwardBy(absl::Milliseconds(500));
  EXPECT_TRUE(logged);
  EXPECT_FALSE(log_success);
  EXPECT_TRUE(transport->state->cancelled);
}

}
}
