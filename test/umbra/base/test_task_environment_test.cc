#include "umbra/base/test_support/test_task_environment.h"

#include <memory>
#include <array>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "umbra/base/single_thread_task_executor.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/test_support/fake_tick_clock.h"
#include "umbra/base/test_support/waitable_event.h"
#include "umbra/base/tick_clock.h"
#include "umbra/base/time_ticks.h"

namespace umbra::base {
namespace {

std::string At(const TestTaskEnvironment& environment, std::string label) {
  return std::to_string(
             absl::ToInt64Milliseconds(environment.NowTicks().SinceOrigin())) +
         "ms:" + std::move(label);
}

TEST(TestTaskEnvironmentTest, RunsTasksByDueTimeThenPostingOrder) {
  TestTaskEnvironment environment;
  auto runner = environment.CreateSequencedTaskRunner(TaskPriority::kControl);
  std::vector<std::string> trace;
  runner->PostDelayedTask([&] { trace.push_back(At(environment, "c")); },
                          absl::Milliseconds(30));
  runner->PostDelayedTask([&] { trace.push_back(At(environment, "a")); },
                          absl::Milliseconds(10));
  runner->PostDelayedTask([&] { trace.push_back(At(environment, "b")); },
                          absl::Milliseconds(10));
  runner->PostTask([&] { trace.push_back(At(environment, "now")); });

  environment.RunUntilIdle();
  EXPECT_EQ(trace, (std::vector<std::string>{"0ms:now"}));
  EXPECT_EQ(environment.NextPendingTaskDelay(), absl::Milliseconds(10));

  environment.FastForwardBy(absl::Milliseconds(25));
  EXPECT_EQ(trace, (std::vector<std::string>{"0ms:now", "10ms:a", "10ms:b"}));
  EXPECT_EQ(environment.NowTicks().SinceOrigin(), absl::Milliseconds(25));

  environment.FastForwardBy(absl::Milliseconds(10));
  EXPECT_EQ(trace, (std::vector<std::string>{"0ms:now", "10ms:a", "10ms:b",
                                             "30ms:c"}));
  EXPECT_EQ(environment.NextPendingTaskDelay(), std::nullopt);
}

TEST(TestTaskEnvironmentTest, OrdersAcrossPrioritiesChronologically) {
  TestTaskEnvironment environment;
  auto background =
      environment.CreateSequencedTaskRunner(TaskPriority::kBackground);
  auto control = environment.CreateSequencedTaskRunner(TaskPriority::kControl);
  std::vector<std::string> trace;
  background->PostTask([&] { trace.emplace_back("background"); });
  control->PostTask([&] { trace.emplace_back("control"); });
  environment.RunUntilIdle();
  EXPECT_EQ(trace, (std::vector<std::string>{"background", "control"}));
  background->PostDelayedTask([&] { trace.emplace_back("earlier"); },
                              absl::Milliseconds(1));
  control->PostDelayedTask([&] { trace.emplace_back("later"); },
                           absl::Milliseconds(2));
  environment.clock().Advance(absl::Milliseconds(3));
  environment.RunUntilIdle();
  EXPECT_EQ(trace, (std::vector<std::string>{"background", "control", "earlier",
                                             "later"}));
}

TEST(TestTaskEnvironmentTest, ProductionExecutorRetainsPriorityOrdering) {
  FakeTickClock const clock;
  SingleThreadTaskExecutor executor(clock);
  auto background =
      executor.CreateSequencedTaskRunner(TaskPriority::kBackground);
  auto control = executor.CreateSequencedTaskRunner(TaskPriority::kControl);
  std::vector<std::string> trace;
  background->PostTask([&] { trace.emplace_back("background"); });
  control->PostTask([&] { trace.emplace_back("control"); });
  executor.RunUntilIdle();
  EXPECT_EQ(trace, (std::vector<std::string>{"control", "background"}));
}

TEST(TestTaskEnvironmentTest, FastForwardNeverRewindsTheClock) {
  TestTaskEnvironment environment;
  auto runner = environment.CreateSequencedTaskRunner(TaskPriority::kControl);
  runner->PostTask(
      [&] { environment.clock().Advance(absl::Milliseconds(10)); });
  environment.FastForwardBy(absl::Milliseconds(5));
  EXPECT_EQ(environment.NowTicks().SinceOrigin(), absl::Milliseconds(10));
  environment.FastForwardBy(-absl::Milliseconds(5));
  EXPECT_EQ(environment.NowTicks().SinceOrigin(), absl::Milliseconds(10));
  int ran = 0;
  runner->PostDelayedTask([&] { ++ran; }, absl::Milliseconds(1));
  environment.FastForwardBy(absl::Milliseconds(1));
  EXPECT_EQ(ran, 1);
  EXPECT_EQ(environment.NowTicks().SinceOrigin(), absl::Milliseconds(11));
}

class ObservedClock final : public TickClock {
 public:
  [[nodiscard]] TimeTicks NowTicks() const override {
    EXPECT_FALSE(stopped);
    return {};
  }

  bool stopped = false;
};

TEST(TestTaskEnvironmentTest, ShutdownRejectsPostsWithoutReadingTheClock) {
  ObservedClock clock;
  SingleThreadTaskExecutor executor(clock);
  auto runner = executor.CreateSequencedTaskRunner(TaskPriority::kBackground);
  executor.Shutdown();
  clock.stopped = true;
  EXPECT_FALSE(runner->PostTask([] {}));
  EXPECT_FALSE(runner->PostDelayedTask([] {}, absl::Milliseconds(1)));
  executor.RunUntilIdle();
}

TEST(TestTaskEnvironmentTest, ShutdownRejectsPostsAfterTheClockIsDestroyed) {
  auto clock = std::make_unique<FakeTickClock>();
  SingleThreadTaskExecutor executor(*clock);
  auto runner = executor.CreateSequencedTaskRunner(TaskPriority::kBackground);
  executor.Shutdown();
  clock.reset();
  EXPECT_FALSE(runner->PostTask([] {}));
  EXPECT_FALSE(runner->PostDelayedTask([] {}, absl::Milliseconds(1)));
  executor.RunUntilIdle();
}

TEST(TestTaskEnvironmentTest, ShutdownDrainsWakeUpsAndConcurrentPosts) {
  FakeTickClock const clock;
  WaitableEvent entered;
  WaitableEvent release;
  WaitableEvent shutting_down;
  WaitableEvent shutdown_done;
  int wakes = 0;
  SingleThreadTaskExecutor executor(clock, [&] {
    ++wakes;
    entered.Signal();
    release.Wait();
  });
  auto runner = executor.CreateSequencedTaskRunner(TaskPriority::kBackground);
  std::thread poster([&] { EXPECT_TRUE(runner->PostTask([] {})); });
  EXPECT_TRUE(entered.TimedWait(absl::Seconds(30)));
  std::thread shutdown([&] {
    shutting_down.Signal();
    executor.Shutdown();
    shutdown_done.Signal();
  });
  EXPECT_TRUE(shutting_down.TimedWait(absl::Seconds(30)));
  EXPECT_FALSE(shutdown_done.IsSignaled());
  release.Signal();
  poster.join();
  shutdown.join();
  EXPECT_EQ(wakes, 1);
  EXPECT_FALSE(runner->PostTask([] {}));
}

TEST(TestTaskEnvironmentTest, WakeUpCanPostAndShutDownReentrantly) {
  FakeTickClock const clock;
  SingleThreadTaskExecutor* current = nullptr;
  std::shared_ptr<SequencedTaskRunner> runner;
  int wakes = 0;
  SingleThreadTaskExecutor executor(clock, [&] {
    ++wakes;
    EXPECT_TRUE(runner->PostDelayedTask([] {}, absl::Milliseconds(20)));
    current->Shutdown();
  });
  current = &executor;
  runner = executor.CreateSequencedTaskRunner(TaskPriority::kBackground);
  EXPECT_TRUE(runner->PostTask([] {}));
  EXPECT_EQ(wakes, 1);
  EXPECT_FALSE(runner->PostTask([] {}));
}

TEST(TestTaskEnvironmentTest, RunForStopsWhenTheBudgetIsSpent) {
  TestTaskEnvironment environment;
  auto runner = environment.CreateSequencedTaskRunner(TaskPriority::kControl);
  int ran = 0;
  for (int i = 0; i < 3; ++i) {
    runner->PostTask([&] {
      ++ran;
      environment.clock().Advance(absl::Milliseconds(2));
    });
  }
  EXPECT_TRUE(environment.executor().RunFor(absl::Milliseconds(3)));
  EXPECT_EQ(ran, 2);
  EXPECT_FALSE(environment.executor().RunFor(absl::Milliseconds(3)));
  EXPECT_EQ(ran, 3);
}

TEST(TestTaskEnvironmentTest, WakesTheHostWhenATaskBecomesTheNextOne) {
  FakeTickClock const clock;
  int wake_ups = 0;
  SingleThreadTaskExecutor executor(clock, [&wake_ups] { ++wake_ups; });
  auto runner = executor.CreateSequencedTaskRunner(TaskPriority::kBackground);
  runner->PostDelayedTask([] {}, absl::Milliseconds(20));
  EXPECT_EQ(wake_ups, 1);
  runner->PostDelayedTask([] {}, absl::Milliseconds(30));
  EXPECT_EQ(wake_ups, 1);
  runner->PostDelayedTask([] {}, absl::Milliseconds(10));
  EXPECT_EQ(wake_ups, 2);
  EXPECT_EQ(executor.NextTaskTime(),
            TimeTicks::FromSinceOrigin(absl::Milliseconds(10)));
}

TEST(TestTaskEnvironmentTest, ShutdownDropsPendingTasks) {
  TestTaskEnvironment environment;
  auto runner = environment.CreateSequencedTaskRunner(TaskPriority::kControl);
  bool ran = false;
  auto witness = std::make_shared<int>(0);
  runner->PostDelayedTask([&ran, witness] { ran = true; },
                          absl::Milliseconds(5));
  EXPECT_EQ(witness.use_count(), 2);
  environment.executor().Shutdown();
  EXPECT_EQ(witness.use_count(), 1);
  EXPECT_FALSE(runner->PostTask([] {}));
  environment.FastForwardBy(absl::Milliseconds(10));
  EXPECT_FALSE(ran);
}

std::vector<std::string> RunTimerScenario() {
  TestTaskEnvironment environment;
  auto control = environment.CreateSequencedTaskRunner(TaskPriority::kControl);
  auto audio = environment.CreateSequencedTaskRunner(TaskPriority::kBackground);
  auto video = environment.CreateSequencedTaskRunner(TaskPriority::kBackground);
  std::vector<std::string> trace;

  struct Timer {
    TestTaskEnvironment* environment;
    std::shared_ptr<SequencedTaskRunner> self;
    std::shared_ptr<SequencedTaskRunner> peer;
    std::vector<std::string>* trace;
    std::string name;
    absl::Duration period;

    void Tick(int count) const {
      trace->push_back(At(*environment, name + std::to_string(count)));
      if (count % 2 == 0) {
        const std::string message = name + ">" + std::to_string(count);
        auto* environment_copy = environment;
        auto* trace_copy = trace;
        peer->PostTask([environment_copy, trace_copy, message] {
          trace_copy->push_back(At(*environment_copy, message));
        });
      }
      if (count < 12) {
        const Timer next = *this;
        self->PostDelayedTask([next, count] { next.Tick(count + 1); }, period);
      }
    }
  };

  const std::array<Timer, 3> timers = {{
      {.environment=&environment, .self=control, .peer=audio, .trace=&trace, .name="C", .period=absl::Milliseconds(7)},
      {.environment=&environment, .self=audio, .peer=video, .trace=&trace, .name="A", .period=absl::Milliseconds(5)},
      {.environment=&environment, .self=video, .peer=control, .trace=&trace, .name="V", .period=absl::Milliseconds(3)},
  }};
  for (const Timer& timer : timers) {
    timer.self->PostTask([timer] { timer.Tick(0); });
  }
  environment.FastForwardBy(absl::Milliseconds(200));
  return trace;
}

TEST(TestTaskEnvironmentTest, ChainedTimersProduceAnIdenticalTrace) {
  const std::vector<std::string> reference = RunTimerScenario();
  ASSERT_EQ(reference.size(), (3U * 13U) + (3U * 7U));
  EXPECT_EQ(reference.front(), "0ms:C0");
  for (int iteration = 0; iteration < 1000; ++iteration) {
    ASSERT_EQ(RunTimerScenario(), reference) << "iteration " << iteration;
  }
}

}
}
