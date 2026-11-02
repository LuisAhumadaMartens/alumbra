#include <atomic>
#include <cstddef>
#include <limits>
#include <memory>
#include <string_view>
#include <thread>

namespace {

int heap_buffer_overflow() {
  auto values = std::make_unique<int[]>(1);
  volatile std::size_t index = 1;
  values[index] = 42;
  return values[0];
}

int signed_overflow() {
  volatile int value = std::numeric_limits<int>::max();
  volatile int increment = 1;
  return value + increment;
}

int data_race() {
  constexpr int kAccesses = 1000;
  volatile int value = 0;
  std::atomic<int> arrived{0};
  std::atomic<int> finished{0};
  auto write = [&](int id) {
    arrived.fetch_add(1, std::memory_order_relaxed);
    while (arrived.load(std::memory_order_relaxed) < 2) {
      std::this_thread::yield();
    }
    for (int i = 0; i < kAccesses; ++i) {
      value = id + i;
    }
    finished.fetch_add(1, std::memory_order_relaxed);
    while (finished.load(std::memory_order_relaxed) < 2) {
      std::this_thread::yield();
    }
  };
  std::thread first(write, 1);
  std::thread second(write, 2);
  first.join();
  second.join();
  return value;
}

}

int main(int argc, char** argv) {
  if (argc != 2) {
    return 2;
  }
  const std::string_view canary(argv[1]);
  if (canary == "heap-buffer-overflow") {
    return heap_buffer_overflow();
  }
  if (canary == "signed-overflow") {
    return signed_overflow();
  }
  if (canary == "data-race") {
    return data_race();
  }
  return 2;
}
