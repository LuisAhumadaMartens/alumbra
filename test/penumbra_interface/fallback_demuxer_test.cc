#include "penumbra/interface/fallback_demuxer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "fake_port.h"
#include "gtest/gtest.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/port.h"
#include "penumbra/interface/port_types.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/test_support/test_task_environment.h"

namespace penumbra::test {
namespace {

static_assert(std::is_base_of_v<FallbackDemuxerProvider, Port>);

class EmptySource final : public FallbackByteSource {
 public:
  absl::StatusOr<size_t> ReadAt(uint64_t offset,
                                std::span<uint8_t> destination) override {
    static_cast<void>(offset);
    static_cast<void>(destination);
    return size_t{0};
  }
  [[nodiscard]] std::optional<uint64_t> Size() const override { return 0; }
  [[nodiscard]] bool Seekable() const override { return true; }
};

class IgnoredCallbacks final : public FallbackDemuxerCallbacks {
 public:
  absl::Status OnOpen(FallbackDemuxerInfo info) override {
    ++calls;
    static_cast<void>(info);
    return absl::OkStatus();
  }
  absl::Status OnSamples(SeekTicket ticket, uint32_t track_id,
                         std::vector<EncodedSample> samples) override {
    ++calls;
    static_cast<void>(ticket);
    static_cast<void>(track_id);
    static_cast<void>(samples);
    return absl::OkStatus();
  }
  absl::Status OnEndOfStream(SeekTicket ticket, uint32_t track_id) override {
    ++calls;
    static_cast<void>(ticket);
    static_cast<void>(track_id);
    return absl::OkStatus();
  }
  absl::Status OnError(absl::Status error) override {
    ++calls;
    static_cast<void>(error);
    return absl::OkStatus();
  }
  size_t calls = 0;
};

class FallbackDemuxerContractTest : public ::testing::Test {
 protected:
  FallbackDemuxerContractTest()
      : control_(std::make_shared<RunnerAdapter>(
            environment_.CreateSequencedTaskRunner(
                umbra::base::TaskPriority::kControl))),
        port_(control_) {}

  umbra::base::TestTaskEnvironment environment_;
  std::shared_ptr<RunnerAdapter> control_;
  FakePort port_;
};

TEST_F(FallbackDemuxerContractTest, FakePortReportsTheProbeUnsupported) {
  const std::array<uint8_t, 4> window = {'R', 'I', 'F', 'F'};
  const auto probe = port_.ProbeFallbackDemuxer(window, "clip.avi");
  EXPECT_EQ(probe.status().code(), absl::StatusCode::kUnimplemented);
}

TEST_F(FallbackDemuxerContractTest, FakePortReportsTheOpenUnsupported) {
  auto source = std::make_shared<EmptySource>();
  auto callbacks = std::make_shared<IgnoredCallbacks>();
  const auto opened = port_.OpenFallbackDemuxer(
      {.initial_ticket = 1, .name_hint = "clip.avi"}, source, callbacks);
  EXPECT_EQ(opened.status().code(), absl::StatusCode::kUnimplemented);
}

TEST_F(FallbackDemuxerContractTest, AnUnsupportedOpenRetainsNoRecipient) {
  auto source = std::make_shared<EmptySource>();
  auto callbacks = std::make_shared<IgnoredCallbacks>();
  const auto opened = port_.OpenFallbackDemuxer(
      {.initial_ticket = 1, .name_hint = ""}, source, callbacks);
  EXPECT_FALSE(opened.ok());
  EXPECT_EQ(source.use_count(), 1);
  EXPECT_EQ(callbacks.use_count(), 1);
  EXPECT_EQ(callbacks->calls, 0U);
}

TEST_F(FallbackDemuxerContractTest, TheProbeIsStableAcrossCalls) {
  const std::array<uint8_t, 4> window = {'O', 'g', 'g', 'S'};
  const auto first = port_.ProbeFallbackDemuxer(window, "");
  const auto second = port_.ProbeFallbackDemuxer({}, "");
  EXPECT_EQ(first.status().code(), second.status().code());
}

}
}
