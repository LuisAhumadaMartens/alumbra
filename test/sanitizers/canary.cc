#include <atomic>
#include <barrier>
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
  int value = 0;
  std::barrier gate(3);
  auto write = [&] {
    gate.arrive_and_wait();
    value = 42;
  };
  std::thread first(write);
  std::thread second(write);
  gate.arrive_and_wait();
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
