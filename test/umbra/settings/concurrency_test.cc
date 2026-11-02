#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "umbra/settings/platform.h"
#include "umbra/settings/settings_keys.h"
#include "umbra/settings/store.h"

namespace umbra::settings {
namespace {

constexpr int kReaders = 4;
constexpr int kWrites = 2000;

int CountInconsistentReads(const Store& store) {
  int violations = 0;
  const std::int64_t attempts = store.Get(kNetworkRetryMaxAttempts);
  if (attempts < 0 || attempts > kWrites) {
    ++violations;
  }
  const std::vector<std::string> categories =
      store.Get(kObservabilityLogCategories);
  if (categories.size() > 2 ||
      (categories.size() == 2 && categories[0] == categories[1])) {
    ++violations;
  }
  if (store.TakeSnapshot().Get(kNetworkRetryMaxAttempts) < 0) {
    ++violations;
  }
  return violations;
}

void WriteUserLayer(Store& store) {
  for (int i = 0; i < kWrites; ++i) {
    EXPECT_TRUE(store.Set(Layer::kUser, kNetworkRetryMaxAttempts, i).ok());
    EXPECT_TRUE(store
                    .Set(Layer::kIntegrator, kObservabilityLogCategories,
                         i % 2 == 0 ? std::vector<std::string>{"net"}
                                    : std::vector<std::string>{"net", "abr"})
                    .ok());
    if (i % 7 == 0) {
      store.Reset(Layer::kUser);
    }
  }
}

void WriteSourceLayer(Store& store) {
  for (int i = 0; i < kWrites; ++i) {
    EXPECT_TRUE(store.Set(Layer::kSource, kNetworkRetryMaxAttempts, i).ok());
    store.Reset(Layer::kSource, kNetworkRetryMaxAttempts);
  }
}

TEST(StoreConcurrencyTest, ReadersSeeConsistentValuesWhileLayersAreUpdated) {
  Store store(GetRegistry(), Platform::kLinux);
  std::atomic<bool> done{false};
  std::atomic<int> violations{0};
  std::vector<std::thread> readers;
  readers.reserve(kReaders);
  for (int reader = 0; reader < kReaders; ++reader) {
    readers.emplace_back([&] {
      while (!done.load(std::memory_order_acquire)) {
        violations.fetch_add(CountInconsistentReads(store));
      }
    });
  }
  std::thread user_writer([&] { WriteUserLayer(store); });
  std::thread source_writer([&] { WriteSourceLayer(store); });
  user_writer.join();
  source_writer.join();
  done.store(true, std::memory_order_release);
  for (auto& reader : readers) {
    reader.join();
  }
  EXPECT_EQ(violations.load(), 0);
}

}
}
