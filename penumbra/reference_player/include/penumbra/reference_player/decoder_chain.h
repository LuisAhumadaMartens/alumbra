#ifndef PENUMBRA_REFERENCE_PLAYER_DECODER_CHAIN_H_
#define PENUMBRA_REFERENCE_PLAYER_DECODER_CHAIN_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/decoder_types.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/task_runner.h"
#include "penumbra/reference_player/decoder_blocklist.h"
#include "penumbra/reference_player/decoder_blocklist_table.h"
#include "penumbra/reference_player/drop_attribution.h"

namespace penumbra {

enum class FallbackReason : uint8_t {
  kConfigurationFailed,
  kErrorBeforeFirstOutput,
  kReconfigurationFailed,
  kDroppedFrames,
};

struct DecoderFallbackEvent {
  std::string previous;
  std::string next;
  FallbackReason reason = FallbackReason::kConfigurationFailed;
  absl::Status cause;
};

class DecoderChainObserver {
 public:
  virtual ~DecoderChainObserver() = default;
  virtual void OnFallback(const DecoderFallbackEvent& event) = 0;
  virtual void OnDropTrigger(const DropTriggerEvent& event) = 0;
};

struct DecoderChainConfig {
  ComponentFactory* factory = nullptr;
  const CapabilityProvider* capabilities = nullptr;
  std::shared_ptr<ControlTaskRunner> control;
  std::shared_ptr<DecoderCallbacks> downstream;
  std::shared_ptr<DecoderChainObserver> observer;
  StreamConfig stream;
  CapabilityRequest capability_request;
  bool require_secure = false;
  DecoderSettings settings;
  DeviceProfile device;
  std::span<const BlocklistEntry> blocklist = kDecoderBlocklist;
  size_t max_retained_samples = 512;
};

struct DecoderChainStats {
  std::string decoder_name;
  DecoderKind kind = DecoderKind::kBundledSoftware;
  DecoderOutput output = DecoderOutput::kCpuBuffer;
  uint64_t decoded = 0;
  uint64_t corrupted = 0;
  uint64_t dropped_decode_late = 0;
  uint64_t dropped_present_late = 0;
  uint64_t fallbacks = 0;
  uint64_t recreations = 0;
  uint64_t drop_triggers = 0;
  size_t retained_samples = 0;

  [[nodiscard]] uint64_t Dropped() const {
    return dropped_decode_late + dropped_present_late;
  }
};

class DecoderChainCore;

class DecoderChain final : public Decoder {
 public:
  static absl::StatusOr<std::unique_ptr<DecoderChain>> Create(
      DecoderChainConfig config, SeekTicket ticket);

  DecoderChain(const DecoderChain&) = delete;
  DecoderChain& operator=(const DecoderChain&) = delete;
  ~DecoderChain() override;

  absl::Status WriteSamples(SeekTicket ticket,
                            std::span<const EncodedSample> samples) override;
  absl::Status EndOfStream(SeekTicket ticket) override;
  absl::Status Flush(SeekTicket ticket) override;
  absl::Status Stop() override;

  absl::Status SwitchToNextCandidate();
  void RecordFrameDue(TimeDelta now);
  void RecordDroppedFrame(DropCause cause, TimeDelta now);
  void PollDropTrigger(TimeDelta now);
  void UpdateDropThresholds(int32_t max_percent, TimeDelta window);
  [[nodiscard]] DecoderChainStats GetStats() const;
  [[nodiscard]] absl::Status GetDeliveryStatus() const;

 private:
  explicit DecoderChain(std::shared_ptr<DecoderChainCore> core);

  std::shared_ptr<DecoderChainCore> core_;
};

}

#endif
