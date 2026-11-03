#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "audio_test_support.h"
#include "penumbra/desktop/audio/audio_renderer.h"

namespace {

std::atomic<int64_t>& TrackedAllocations() {
  static std::atomic<int64_t> count{0};
  return count;
}

bool& Tracking() {
  thread_local bool tracking = false;
  return tracking;
}

void* RequireAllocation(void* pointer) {
  if (pointer == nullptr) {
    throw std::bad_alloc();
  }
  return pointer;
}

void* TrackedAllocate(std::size_t size) {
  if (Tracking()) {
    TrackedAllocations().fetch_add(1, std::memory_order_relaxed);
  }
  return RequireAllocation(__builtin_malloc(size == 0 ? 1 : size));
}

}

void* operator new(std::size_t size) { return TrackedAllocate(size); }
void* operator new[](std::size_t size) { return TrackedAllocate(size); }
void operator delete(void* pointer) noexcept { __builtin_free(pointer); }
void operator delete[](void* pointer) noexcept { __builtin_free(pointer); }
void operator delete(void* pointer, std::size_t /*size*/) noexcept {
  __builtin_free(pointer);
}
void operator delete[](void* pointer, std::size_t /*size*/) noexcept {
  __builtin_free(pointer);
}

namespace penumbra::desktop {
namespace {

using testing::ManualClock;
using testing::ScriptedSource;

constexpr int32_t kRate = 48000;
constexpr size_t kMediumFrames = 1024;
constexpr size_t kTinyFrames = 3;
constexpr size_t kBigFrames = 9000;

TEST(AudioRendererAllocationTest, RenderingDoesNotAllocateOrBlock) {
  ManualClock clock;
  auto source = std::make_shared<ScriptedSource>(2, 4);
  AudioRendererConfig config;
  config.sample_rate = kRate;
  config.channel_count = 2;
  absl::StatusOr<std::unique_ptr<AudioRenderer>> created =
      AudioRenderer::Create(config, source, &clock);
  ASSERT_TRUE(created.ok());
  const std::unique_ptr<AudioRenderer> renderer_holder = *std::move(created);
  AudioRenderer* renderer = renderer_holder.get();
  source->SetMode(ScriptedSource::Mode::kCounter);
  std::vector<float> output(kMediumFrames * 2);
  std::vector<float> big(kBigFrames * 2);
  TrackedAllocations().store(0);
  std::thread audio([&] {
    Tracking() = true;
    for (int round = 0; round < 200; ++round) {
      renderer->Render(std::span<float>(output).first(kMediumFrames * 2));
      renderer->Render(std::span<float>(output).first(kTinyFrames * 2));
      renderer->Render(big);
    }
    Tracking() = false;
  });
  for (int round = 0; round < 50; ++round) {
    renderer->SetVolume(round % 2 == 0 ? 0.3F : 1.0F);
    renderer->SetMuted(round % 3 == 0);
    renderer->Pause();
    static_cast<void>(renderer->EstimatePlayedFrames(1'000'000'000, 480));
    renderer->Resume();
  }
  audio.join();
  EXPECT_EQ(TrackedAllocations().load(), 0);
}

}
}
