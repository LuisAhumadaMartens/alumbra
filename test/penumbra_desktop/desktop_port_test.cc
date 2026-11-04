#include "penumbra/desktop/desktop_port.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "penumbra/interface/interface.h"
#include "penumbra/interface/port.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/task_runner.h"

namespace penumbra::desktop {
namespace {

DesktopPortOptions HeadlessOptions() {
  DesktopPortOptions options;
  options.headless = true;
  return options;
}

TEST(DesktopPortTest, CreatesHeadlessPortWithCurrentContractVersion) {
  auto port = CreatePort(HeadlessOptions());
  ASSERT_TRUE(port.ok()) << port.status();
  auto limits = (*port)->GetLimits();
  ASSERT_TRUE(limits.ok()) << limits.status();
  EXPECT_EQ(limits->api_version, kApiVersion);
  EXPECT_EQ(limits->max_players, 1U);
}

TEST(DesktopPortTest, ExposesFileAndHttpByteStreamProviders) {
  auto port = CreatePort(HeadlessOptions());
  ASSERT_TRUE(port.ok()) << port.status();
  const auto providers = (*port)->ByteStreamProviders();
  ASSERT_EQ(providers.size(), 2U);
  EXPECT_EQ(providers[0]->Schemes(), (std::vector<std::string>{"file"}));
  EXPECT_EQ(providers[1]->Schemes(),
            (std::vector<std::string>{"http", "https"}));
  EXPECT_TRUE((*port)->ElementaryStreamProviders().empty());
}

TEST(DesktopPortTest, ExposesFallbackDemuxerProvider) {
  auto port = CreatePort(HeadlessOptions());
  ASSERT_TRUE(port.ok()) << port.status();
  const auto probe = (*port)->ProbeFallbackDemuxer({}, "");
  ASSERT_TRUE(probe.ok()) << probe.status();
  EXPECT_FALSE(probe->supported);
}

TEST(DesktopPortTest, RunsPostedTasksOnItsTaskRunnerSequence) {
  auto port = CreatePort(HeadlessOptions());
  ASSERT_TRUE(port.ok()) << port.status();
  std::mutex mutex;
  std::condition_variable condition;
  bool completed = false;
  bool on_sequence = false;
  auto runner = (*port)->CreateTaskRunner(TaskPriority::kControl,
                                          BlockingPolicy::kNonBlocking);
  ASSERT_TRUE(runner.ok()) << runner.status();
  ASSERT_TRUE((*runner)
                  ->PostTask(
                      [&] {
                        {
                          const std::scoped_lock lock(mutex);
                          completed = true;
                          on_sequence = (*runner)->RunsTasksInCurrentSequence();
                          condition.notify_one();
                        }
                      },
                      absl::ZeroDuration())
                  .ok());
  std::unique_lock lock(mutex);
  condition.wait(lock, [&] { return completed; });
  EXPECT_TRUE(on_sequence);
}

TEST(DesktopPortTest, ReturnsConfiguredBufferBudgets) {
  BufferBudgets expected;
  expected.video_max_bytes = 32U * 1024U * 1024U;
  expected.audio_max_bytes = 4U * 1024U * 1024U;
  DesktopPortOptions options = HeadlessOptions();
  options.buffer_budgets = expected;
  auto port = CreatePort(std::move(options));
  ASSERT_TRUE(port.ok()) << port.status();
  const BufferBudgets actual = (*port)->GetBufferBudgets();
  EXPECT_EQ(actual.video_max_bytes, expected.video_max_bytes);
  EXPECT_EQ(actual.audio_max_bytes, expected.audio_max_bytes);
}

}
}
