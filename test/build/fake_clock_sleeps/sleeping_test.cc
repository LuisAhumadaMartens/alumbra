#include <chrono>
#include <thread>

#include "umbra/base/test_support/test_task_environment.h"

void WaitsForRealTime() {
  umbra::base::TestTaskEnvironment environment;
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
}
