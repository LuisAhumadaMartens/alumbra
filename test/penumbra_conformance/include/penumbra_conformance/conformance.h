#ifndef TEST_PENUMBRA_CONFORMANCE_CONFORMANCE_H_
#define TEST_PENUMBRA_CONFORMANCE_CONFORMANCE_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "penumbra/interface/callback_dispatcher.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/task_runner.h"
#include "penumbra_conformance/port_harness.h"
#include "umbra/base/test_support/test_task_environment.h"

namespace penumbra::conformance {

inline constexpr uint32_t kVideoTrack = 7;
inline constexpr uint32_t kAudioTrack = 8;

StreamConfig VideoStream(uint32_t track = kVideoTrack);
StreamConfig AudioStream(uint32_t track = kAudioTrack);
PlayerConfig DefaultPlayerConfig();
EncodedSample MakeSample(uint32_t track, int64_t index);

class RecordingReceiver final : public PlayerCallbacks {
 public:
  explicit RecordingReceiver(std::shared_ptr<ControlTaskRunner> control);
  absl::Status OnEvent(PlayerEvent event) override;
  [[nodiscard]] std::vector<PlayerEvent> Events() const;
  [[nodiscard]] size_t OutsideCallbackScope() const;
  [[nodiscard]] size_t OffControlSequence() const;
  bool ConsumeGrant(uint32_t track_id, SeekTicket ticket, size_t* max_samples);

 private:
  std::shared_ptr<ControlTaskRunner> control_;
  mutable std::mutex mutex_;
  std::vector<PlayerEvent> events_;
  std::vector<bool> consumed_;
  size_t outside_scope_ = 0;
  size_t off_control_ = 0;
};

class RecordingDecoderReceiver final : public DecoderCallbacks {
 public:
  absl::Status OnDecoded(DecoderEvent event) override;
  [[nodiscard]] std::vector<DecoderEvent> Events() const;
  [[nodiscard]] size_t OutsideCallbackScope() const;
  bool ConsumeGrant(SeekTicket ticket, size_t* max_samples);

 private:
  mutable std::mutex mutex_;
  std::vector<DecoderEvent> events_;
  std::vector<bool> consumed_;
  size_t outside_scope_ = 0;
};

class PortConformanceTest : public ::testing::TestWithParam<PortCase> {
 protected:
  void SetUp() override;
  void TearDown() override;
  [[nodiscard]] static bool Has(Capability capability);
  void Run(ControlTaskRunner::Task task);
  void Settle();
  absl::Status Create(PlayerConfig config = {});
  absl::Status CreateExtra(PlayerConfig config = {});
  absl::Status TryCreate(PlayerConfig config,
                         std::shared_ptr<PlayerCallbacks> callbacks);
  void SetTicket(SeekTicket ticket) { ticket_ = ticket; }
  bool Grant(uint32_t track_id, size_t max_samples = 1);
  absl::Status DoSeek(SeekTicket ticket, MediaTime position);
  absl::Status DoFlush(SeekTicket ticket);
  void DestroyPlayer();

  PortHarness& Harness() { return *harness_; }
  Player& CurrentPlayer() { return *player_; }
  RecordingReceiver& Receiver() { return *receiver_; }
  CallbackDispatcher& Dispatcher() { return *dispatcher_; }
  [[nodiscard]] SeekTicket Ticket() const { return ticket_; }
  const std::shared_ptr<ControlTaskRunner>& Control() const { return control_; }

 private:
  umbra::base::TestTaskEnvironment environment_;
  std::shared_ptr<ControlTaskRunner> control_;
  std::unique_ptr<PortHarness> harness_;
  std::shared_ptr<RecordingReceiver> receiver_;
  std::shared_ptr<CallbackDispatcher> dispatcher_;
  std::unique_ptr<Player> player_;
  std::vector<std::unique_ptr<Player>> extra_players_;
  std::vector<std::shared_ptr<CallbackDispatcher>> extra_dispatchers_;
  SeekTicket ticket_ = 1;
};

#define ALUMBRA_PORT_REQUIRE(capability)                                      \
  do {                                                                        \
    if (!Has(capability)) {                                                   \
      GTEST_SKIP() << "port does not declare " << CapabilityName(capability); \
    }                                                                         \
  } while (false)

#define ALUMBRA_PORT_CONFORMANCE_SUITES(X, ...) \
  X(LifecycleConformance, __VA_ARGS__)          \
  X(FlowControlConformance, __VA_ARGS__)        \
  X(SeekTicketConformance, __VA_ARGS__)         \
  X(ClockConformance, __VA_ARGS__)              \
  X(CallbackThreadingConformance, __VA_ARGS__)  \
  X(ErrorReportingConformance, __VA_ARGS__)     \
  X(CapabilityConformance, __VA_ARGS__)         \
  X(OutputConformance, __VA_ARGS__)             \
  X(ComponentConformance, __VA_ARGS__)

#define ALUMBRA_PORT_CONFORMANCE_DECLARE(suite, ...) \
  class suite : public PortConformanceTest {};

ALUMBRA_PORT_CONFORMANCE_SUITES(ALUMBRA_PORT_CONFORMANCE_DECLARE)

#define ALUMBRA_PORT_CONFORMANCE_INSTANTIATE_ONE(suite, prefix, cases) \
  using ::penumbra::conformance::suite;                                \
  INSTANTIATE_TEST_SUITE_P(prefix, suite, ::testing::ValuesIn(cases),  \
                           ::penumbra::conformance::PortCaseName);

#define ALUMBRA_INSTANTIATE_PORT_CONFORMANCE(prefix, cases)                 \
  ALUMBRA_PORT_CONFORMANCE_SUITES(ALUMBRA_PORT_CONFORMANCE_INSTANTIATE_ONE, \
                                  prefix, cases)

}

#endif
