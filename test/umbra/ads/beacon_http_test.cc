#include <memory>
#include <string>
#include <utility>

#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "http_test_server.h"
#include "penumbra/desktop/curl_http_transport.h"
#include "umbra/ads/beacon_dispatcher.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/test_support/waitable_event.h"
#include "umbra/base/thread_pool.h"

namespace umbra::ads {
namespace {

TEST(BeaconHttpTest, SendsGetThroughDesktopTransportToLocalServer) {
  auto server_result = penumbra::desktop::test::HttpTestServer::Start(
      false, [](penumbra::desktop::test::ServerConnection& connection,
                const penumbra::desktop::test::ServerRequest&) {
        penumbra::desktop::test::SendResponse(connection, 204, {}, {});
      });
  ASSERT_TRUE(server_result.ok()) << server_result.status();
  std::unique_ptr<penumbra::desktop::test::HttpTestServer> server =
      std::move(*server_result);
  auto transport_result = penumbra::desktop::CurlHttpTransport::Create();
  ASSERT_TRUE(transport_result.ok()) << transport_result.status();
  const std::shared_ptr<penumbra::desktop::CurlHttpTransport> transport =
      std::move(*transport_result);
  umbra::base::ThreadPool pool(
      {.max_workers = 1, .thread_factory = nullptr, .tick_clock = nullptr});
  auto runner =
      pool.CreateSequencedTaskRunner(umbra::base::TaskPriority::kBackground);
  umbra::base::WaitableEvent completed;
  bool success = false;
  int status = 0;
  BeaconDispatcher dispatcher(
      transport, runner, absl::Seconds(2),
      [&completed, &success, &status](bool result, int response_status) {
        success = result;
        status = response_status;
        completed.Signal();
      });
  dispatcher.Dispatch(server->BaseUrl() + "/beacon");
  ASSERT_TRUE(completed.TimedWait(absl::Seconds(5)));
  EXPECT_TRUE(success);
  EXPECT_EQ(status, 204);
  const auto requests = server->Requests();
  ASSERT_EQ(requests.size(), 1U);
  EXPECT_EQ(requests.front().method, "GET");
  EXPECT_EQ(requests.front().target, "/beacon");
  pool.Shutdown();
  EXPECT_TRUE(transport->Stop().ok());
  server->Stop();
}

}
}
